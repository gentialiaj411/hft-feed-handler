#!/usr/bin/env python3
"""Minimal client for the recorded-data TickReplayService (not live market data)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import grpc

# Stubs: run scripts/gen_tick_proto_py.sh after pip install -r python/requirements.txt
_GEN = Path(__file__).resolve().parent / "gen"
if _GEN.is_dir():
    sys.path.insert(0, str(_GEN))
try:
    import tick_service_pb2 as pb2
    import tick_service_pb2_grpc as pb2_grpc
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Missing generated stubs. Run:\n"
        "  pip install -r python/requirements.txt\n"
        "  bash scripts/gen_tick_proto_py.sh\n"
    ) from exc


def _channel(addr: str) -> grpc.Channel:
    return grpc.insecure_channel(addr)


def cmd_health(args: argparse.Namespace) -> int:
    stub = pb2_grpc.TickReplayServiceStub(_channel(args.addr))
    resp = stub.Health(pb2.HealthRequest(), timeout=args.timeout)
    print(f"mode={resp.service_mode}")
    print(f"book={resp.book_journal_path} records={resp.book_record_count}")
    print(f"nbbo={resp.nbbo_journal_path} records={resp.nbbo_record_count}")
    print(f"uptime_s={resp.uptime_seconds} build={resp.build_id}")
    return 0


def cmd_stream(args: argparse.Namespace) -> int:
    stub = pb2_grpc.TickReplayServiceStub(_channel(args.addr))
    req = pb2.StreamTicksRequest(
        symbol=args.symbol,
        start_ts_ns=args.start_ts,
        end_ts_ns=args.end_ts,
        limit=args.limit,
    )
    count = 0
    for ev in stub.StreamTicks(req, timeout=args.timeout):
        if count < args.print_n:
            print(
                f"seq={ev.sequence} ts={ev.exchange_ts_ns} sym={ev.symbol} "
                f"type={ev.event_type} px={ev.price} qty={ev.qty}"
            )
        count += 1
    print(f"streamed_ticks={count}")
    return 0


def cmd_nbbo(args: argparse.Namespace) -> int:
    stub = pb2_grpc.TickReplayServiceStub(_channel(args.addr))
    if args.stream:
        req = pb2.StreamNbboRequest(
            symbol=args.symbol,
            start_ts_ns=args.start_ts,
            end_ts_ns=args.end_ts,
            limit=args.limit,
        )
        count = 0
        for row in stub.StreamNbbo(req, timeout=args.timeout):
            if count < args.print_n:
                print(
                    f"ts={row.exchange_ts_ns} sym={row.symbol} "
                    f"bid={row.bid_price}x{row.bid_qty} ask={row.ask_price}x{row.ask_qty}"
                )
            count += 1
        print(f"streamed_nbbo={count}")
        return 0

    req = pb2.QueryNbboRequest(symbol=args.symbol, timestamp_ns=args.ts)
    snap = stub.QueryNbbo(req, timeout=args.timeout)
    if not snap.found:
        print("nbbo_not_found")
        return 1
    n = snap.nbbo
    print(
        f"ts={n.exchange_ts_ns} sym={n.symbol} "
        f"bid={n.bid_price}x{n.bid_qty} ask={n.ask_price}x{n.ask_qty}"
    )
    return 0


def main() -> int:
    p = argparse.ArgumentParser(description="MultiFeed recorded replay gRPC client")
    p.add_argument("--addr", default="127.0.0.1:50051")
    p.add_argument("--timeout", type=float, default=120.0)
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("health").set_defaults(func=cmd_health)

    ps = sub.add_parser("stream")
    ps.add_argument("--symbol", default="")
    ps.add_argument("--start-ts", type=int, default=0, dest="start_ts")
    ps.add_argument("--end-ts", type=int, default=0, dest="end_ts")
    ps.add_argument("--limit", type=int, default=1000)
    ps.add_argument("--print-n", type=int, default=5)
    ps.set_defaults(func=cmd_stream)

    pn = sub.add_parser("nbbo")
    pn.add_argument("--symbol", required=True)
    pn.add_argument("--ts", type=int, default=0, help="point lookup timestamp (ns)")
    pn.add_argument("--stream", action="store_true")
    pn.add_argument("--start-ts", type=int, default=0, dest="start_ts")
    pn.add_argument("--end-ts", type=int, default=0, dest="end_ts")
    pn.add_argument("--limit", type=int, default=1000)
    pn.add_argument("--print-n", type=int, default=5)
    pn.set_defaults(func=cmd_nbbo)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
