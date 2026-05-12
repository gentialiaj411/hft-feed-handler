#!/usr/bin/env python3
"""
validate_feature_math.py — independent validation of the six microstructure
feature formulas implemented in src/phase3/feature_pipeline.cpp.

Each test case is self-contained: it constructs a synthetic NBBO + trade
sequence, runs a pure-Python reference implementation of the same algorithm,
and asserts the result matches the expected analytic value within tolerance.
No C++ binary is required; this validates the mathematical specification.

Run:
    python3 scripts/validate_feature_math.py
"""

from __future__ import annotations

import math
import sys
from collections import deque
from dataclasses import dataclass, field
from typing import Optional

PASS = "\033[32mPASS\033[0m"
FAIL = "\033[31mFAIL\033[0m"


# ── Data types (mirrors C++ structs) ─────────────────────────────────────────

@dataclass
class Nbbo:
    has_bid: bool = False
    has_ask: bool = False
    bid_price: int = 0
    bid_qty: int = 0
    ask_price: int = 0
    ask_qty: int = 0


@dataclass
class BookEvent:
    exchange_ts_ns: int = 0
    is_trade: bool = False
    trade_price: int = 0   # only when is_trade
    signed_qty: float = 0.0  # +qty buy / -qty sell (only when is_trade)


@dataclass
class FeaturePipelineState:
    """Mirrors FeaturePipeline::State in feature_pipeline.hpp."""
    last_bid_price: int = 0
    last_bid_qty: int = 0
    last_ask_price: int = 0
    last_ask_qty: int = 0

    ofi_sum: float = 0.0
    ofi_window: deque = field(default_factory=deque)

    effective_spread_ema: float = 0.0
    has_effective_spread: bool = False

    last_mid: float = 0.0
    has_mid: bool = False

    reg_n: int = 0
    reg_sum_x: float = 0.0
    reg_sum_y: float = 0.0
    reg_sum_xx: float = 0.0
    reg_sum_xy: float = 0.0

    vpin_bucket_buy: int = 0
    vpin_bucket_sell: int = 0
    vpin_buckets: deque = field(default_factory=deque)


OFI_WINDOW_NS = 1_000_000_000  # 1 s
VPIN_BUCKET_VOLUME = 10_000
VPIN_BUCKET_COUNT = 32


def run_feature_pipeline(
    nbbo: Nbbo,
    ev: BookEvent,
    s: FeaturePipelineState,
) -> Optional[dict]:
    """Exact Python mirror of FeaturePipeline::on_event."""
    if not nbbo.has_bid or not nbbo.has_ask or nbbo.bid_price == 0 or nbbo.ask_price == 0:
        return None

    bid_p = float(nbbo.bid_price)
    ask_p = float(nbbo.ask_price)
    bid_q = float(nbbo.bid_qty)
    ask_q = float(nbbo.ask_qty)
    mid = 0.5 * (bid_p + ask_p)
    denom = bid_q + ask_q
    micro = (ask_p * bid_q + bid_p * ask_q) / denom if denom > 0.0 else mid

    # OFI delta
    delta_ofi = 0.0
    if s.last_bid_price != 0 or s.last_ask_price != 0:
        if nbbo.bid_price > s.last_bid_price:
            delta_ofi += bid_q
        if nbbo.bid_price < s.last_bid_price:
            delta_ofi -= float(s.last_bid_qty)
        if nbbo.ask_price < s.last_ask_price:
            delta_ofi -= ask_q
        if nbbo.ask_price > s.last_ask_price:
            delta_ofi += float(s.last_ask_qty)
        delta_ofi += (bid_q - float(s.last_bid_qty))
        delta_ofi -= (ask_q - float(s.last_ask_qty))

    s.last_bid_price = nbbo.bid_price
    s.last_bid_qty = nbbo.bid_qty
    s.last_ask_price = nbbo.ask_price
    s.last_ask_qty = nbbo.ask_qty

    s.ofi_sum += delta_ofi
    s.ofi_window.append((ev.exchange_ts_ns, delta_ofi))
    while s.ofi_window and (ev.exchange_ts_ns - s.ofi_window[0][0]) > OFI_WINDOW_NS:
        s.ofi_sum -= s.ofi_window[0][1]
        s.ofi_window.popleft()

    # Effective spread + Kyle's lambda + VPIN (on trades only)
    if ev.is_trade:
        trade_p = float(ev.trade_price)
        eff = 2.0 * abs(trade_p - mid)
        if not s.has_effective_spread:
            s.effective_spread_ema = eff
            s.has_effective_spread = True
        else:
            s.effective_spread_ema = 0.95 * s.effective_spread_ema + 0.05 * eff

        x = ev.signed_qty
        y = (mid - s.last_mid) if s.has_mid else 0.0
        s.reg_n += 1
        s.reg_sum_x += x
        s.reg_sum_y += y
        s.reg_sum_xx += x * x
        s.reg_sum_xy += x * y

        signed_qty_abs = abs(ev.signed_qty)
        if ev.signed_qty > 0.0:
            s.vpin_bucket_buy += int(signed_qty_abs)
        elif ev.signed_qty < 0.0:
            s.vpin_bucket_sell += int(signed_qty_abs)

        while (s.vpin_bucket_buy + s.vpin_bucket_sell) >= VPIN_BUCKET_VOLUME:
            tox = abs(s.vpin_bucket_buy - s.vpin_bucket_sell) / float(VPIN_BUCKET_VOLUME)
            s.vpin_buckets.append(tox)
            if len(s.vpin_buckets) > VPIN_BUCKET_COUNT:
                s.vpin_buckets.popleft()
            excess = (s.vpin_bucket_buy + s.vpin_bucket_sell) - VPIN_BUCKET_VOLUME
            s.vpin_bucket_buy = excess if s.vpin_bucket_buy > excess else 0
            s.vpin_bucket_sell = (excess - s.vpin_bucket_buy) if s.vpin_bucket_sell > s.vpin_bucket_buy else 0

    s.last_mid = mid
    s.has_mid = True

    # Kyle's lambda (online OLS)
    denom_reg = float(s.reg_n) * s.reg_sum_xx - s.reg_sum_x * s.reg_sum_x
    lambda_ = 0.0
    if abs(denom_reg) > 1e-12:
        lambda_ = (float(s.reg_n) * s.reg_sum_xy - s.reg_sum_x * s.reg_sum_y) / denom_reg

    vpin = 0.0
    if s.vpin_buckets:
        vpin = sum(s.vpin_buckets) / len(s.vpin_buckets)

    return {
        "microprice": micro,
        "ofi": s.ofi_sum,
        "effective_spread": s.effective_spread_ema,
        "kyle_lambda": lambda_,
        "vpin": vpin,
        "mid": mid,
    }


# ── Test harness helpers ──────────────────────────────────────────────────────

results: list[tuple[str, bool, str]] = []


def check(name: str, got: float, expected: float, tol: float = 1e-10) -> None:
    ok = abs(got - expected) <= tol
    results.append((name, ok, f"got={got:.15g}  expected={expected:.15g}  |err|={abs(got-expected):.3e}"))


# ── Test 1: Microprice ────────────────────────────────────────────────────────

def test_microprice() -> None:
    nbbo = Nbbo(has_bid=True, has_ask=True, bid_price=10000, bid_qty=200, ask_price=10200, ask_qty=100)
    ev = BookEvent(exchange_ts_ns=1_000_000_000)
    s = FeaturePipelineState()
    fv = run_feature_pipeline(nbbo, ev, s)
    assert fv is not None

    # (ask * bid_qty + bid * ask_qty) / (bid_qty + ask_qty)
    expected = (10200 * 200 + 10000 * 100) / (200 + 100)
    check("microprice/weighted_mid", fv["microprice"], expected)

    # Degenerate: equal quantities → plain mid
    nbbo2 = Nbbo(has_bid=True, has_ask=True, bid_price=10000, bid_qty=100, ask_price=10200, ask_qty=100)
    fv2 = run_feature_pipeline(nbbo2, BookEvent(exchange_ts_ns=1_500_000_000), FeaturePipelineState())
    assert fv2 is not None
    check("microprice/equal_qty_is_mid", fv2["microprice"], 10100.0)


# ── Test 2: OFI accumulation and window expiry ────────────────────────────────

def test_ofi() -> None:
    s = FeaturePipelineState()

    # t=0: setup — first tick, last_bid/ask both 0 → delta=0
    nbbo0 = Nbbo(has_bid=True, has_ask=True, bid_price=100, bid_qty=50, ask_price=102, ask_qty=30)
    fv0 = run_feature_pipeline(nbbo0, BookEvent(exchange_ts_ns=0), s)
    check("ofi/initial_zero", fv0["ofi"], 0.0)

    # t=100ms: bid improves 100→101, qty 50→60, ask unchanged
    nbbo1 = Nbbo(has_bid=True, has_ask=True, bid_price=101, bid_qty=60, ask_price=102, ask_qty=30)
    fv1 = run_feature_pipeline(nbbo1, BookEvent(exchange_ts_ns=100_000_000), s)
    # delta = +bid_q(60) [price↑] + (60-50) [qty↑] - (30-30) [ask qty unchanged] = 70
    check("ofi/bid_improve", fv1["ofi"], 70.0)

    # t=200ms: ask worsens 102→103, qty 30→20, bid unchanged
    nbbo2 = Nbbo(has_bid=True, has_ask=True, bid_price=101, bid_qty=60, ask_price=103, ask_qty=20)
    fv2 = run_feature_pipeline(nbbo2, BookEvent(exchange_ts_ns=200_000_000), s)
    # delta = +last_ask_qty(30) [ask price↑] + (60-60) [bid qty] - (20-30) [ask qty↓ → -(-10)=+10] = 40
    check("ofi/ask_worsen", fv2["ofi"], 110.0)

    # t=1200ms: NBBO unchanged → delta=0
    # Window evictions (1200ms > 1000ms from t=0 and t=100ms):
    #   t=0 (delta=0): 1200-0=1200ms > 1000ms → evicts (ofi unchanged: -0)
    #   t=100ms (delta=70): 1200-100=1100ms > 1000ms → evicts (ofi 110→40)
    #   t=200ms (delta=40): 1200-200=1000ms NOT > 1000ms → stays
    fv3 = run_feature_pipeline(nbbo2, BookEvent(exchange_ts_ns=1_200_000_000), s)
    check("ofi/window_eviction", fv3["ofi"], 40.0)


# ── Test 3: Effective spread EMA ─────────────────────────────────────────────

def test_effective_spread() -> None:
    s = FeaturePipelineState()
    nbbo = Nbbo(has_bid=True, has_ask=True, bid_price=1000, bid_qty=100, ask_price=1020, ask_qty=100)
    # mid = 1010

    # First trade at 1012: eff = 2*|1012-1010| = 4 → initialise EMA
    ev1 = BookEvent(exchange_ts_ns=1_000_000_000, is_trade=True, trade_price=1012, signed_qty=100.0)
    fv1 = run_feature_pipeline(nbbo, ev1, s)
    check("eff_spread/init", fv1["effective_spread"], 4.0)

    # Second trade at 1008: eff = 2*|1008-1010| = 4 → EMA = 0.95*4 + 0.05*4 = 4
    ev2 = BookEvent(exchange_ts_ns=2_000_000_000, is_trade=True, trade_price=1008, signed_qty=-100.0)
    fv2 = run_feature_pipeline(nbbo, ev2, s)
    check("eff_spread/ema_stable", fv2["effective_spread"], 4.0)

    # Third trade at 1016: eff = 2*|1016-1010| = 12 → EMA = 0.95*4 + 0.05*12 = 3.8 + 0.6 = 4.4
    ev3 = BookEvent(exchange_ts_ns=3_000_000_000, is_trade=True, trade_price=1016, signed_qty=50.0)
    fv3 = run_feature_pipeline(nbbo, ev3, s)
    check("eff_spread/ema_update", fv3["effective_spread"], 4.4)


# ── Test 4: Kyle's lambda (online OLS) ───────────────────────────────────────

def test_kyle_lambda() -> None:
    s = FeaturePipelineState()
    nbbo = Nbbo(has_bid=True, has_ask=True, bid_price=1000, bid_qty=100, ask_price=1020, ask_qty=100)

    # Trade 1: buy 100 @ 1012, mid=1010. y=0 (no prev mid), x=+100
    ev1 = BookEvent(exchange_ts_ns=1_000_000_000, is_trade=True, trade_price=1012, signed_qty=100.0)
    run_feature_pipeline(nbbo, ev1, s)

    # NBBO shift: mid moves to 1015
    nbbo2 = Nbbo(has_bid=True, has_ask=True, bid_price=1010, bid_qty=100, ask_price=1020, ask_qty=100)
    # Trade 2: buy 200 @ 1016, y = 1015-1010=5, x=+200
    ev2 = BookEvent(exchange_ts_ns=2_000_000_000, is_trade=True, trade_price=1016, signed_qty=200.0)
    fv2 = run_feature_pipeline(nbbo2, ev2, s)

    # Manual OLS: n=2, sum_x=300, sum_y=5, sum_xx=100^2+200^2=50000, sum_xy=100*0+200*5=1000
    n, sx, sy, sxx, sxy = 2, 300.0, 5.0, 50000.0, 1000.0
    denom = n * sxx - sx * sx
    expected_lambda = (n * sxy - sx * sy) / denom
    check("kyle_lambda/ols_formula", fv2["kyle_lambda"], expected_lambda)


# ── Test 5: VPIN bucket mechanism ────────────────────────────────────────────

def test_vpin() -> None:
    s = FeaturePipelineState()
    nbbo = Nbbo(has_bid=True, has_ask=True, bid_price=1000, bid_qty=100, ask_price=1020, ask_qty=100)

    # Fill one bucket: 6000 buy + 4000 sell = 10000 (exactly one bucket)
    ev_buy = BookEvent(exchange_ts_ns=1_000_000_000, is_trade=True, trade_price=1010, signed_qty=6000.0)
    run_feature_pipeline(nbbo, ev_buy, s)
    ev_sell = BookEvent(exchange_ts_ns=2_000_000_000, is_trade=True, trade_price=1010, signed_qty=-4000.0)
    fv = run_feature_pipeline(nbbo, ev_sell, s)
    # toxicity = |6000 - 4000| / 10000 = 0.2
    check("vpin/single_bucket", fv["vpin"], 0.2)

    # Fill second bucket: 10000 sell → toxicity = |0 - 10000| / 10000 = 1.0
    ev_sell2 = BookEvent(exchange_ts_ns=3_000_000_000, is_trade=True, trade_price=1010, signed_qty=-10000.0)
    fv2 = run_feature_pipeline(nbbo, ev_sell2, s)
    # Two buckets: [0.2, 1.0] → mean = 0.6
    check("vpin/two_bucket_mean", fv2["vpin"], 0.6)


# ── Test 6: Guard — nullopt when NBBO invalid ─────────────────────────────────

def test_nbbo_guard() -> None:
    s = FeaturePipelineState()
    nbbo_bad = Nbbo(has_bid=False, has_ask=True, bid_price=0, bid_qty=0, ask_price=1020, ask_qty=100)
    ev = BookEvent(exchange_ts_ns=1_000_000_000)
    result = run_feature_pipeline(nbbo_bad, ev, s)
    ok = result is None
    results.append(("nbbo_guard/no_bid_returns_none", ok, "returns None when NBBO incomplete"))

    nbbo_zero = Nbbo(has_bid=True, has_ask=True, bid_price=0, bid_qty=0, ask_price=0, ask_qty=0)
    result2 = run_feature_pipeline(nbbo_zero, ev, FeaturePipelineState())
    ok2 = result2 is None
    results.append(("nbbo_guard/zero_prices_returns_none", ok2, "returns None when prices are zero"))


# ── Runner ────────────────────────────────────────────────────────────────────

def main() -> int:
    print("MultiFeed — feature math validation")
    print("=" * 60)

    test_microprice()
    test_ofi()
    test_effective_spread()
    test_kyle_lambda()
    test_vpin()
    test_nbbo_guard()

    pass_count = sum(1 for _, ok, _ in results if ok)
    fail_count = len(results) - pass_count

    for name, ok, detail in results:
        status = PASS if ok else FAIL
        print(f"  {status}  {name}")
        if not ok:
            print(f"         {detail}")

    print("=" * 60)
    print(f"  {pass_count}/{len(results)} passed", end="")
    if fail_count:
        print(f"  ({fail_count} FAILED)")
        return 1
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
