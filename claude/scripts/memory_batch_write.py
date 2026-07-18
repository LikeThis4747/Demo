"""Write a JSON batch through this project's Memory MCP stdio server."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
from pathlib import Path

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


async def run(project_root: Path, manifest_path: Path) -> None:
    project_root = project_root.resolve()
    memory_root = project_root / "MCP" / "memory"
    python = memory_root / ".venv" / "Scripts" / "python.exe"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    env = dict(os.environ)
    env["PYTHONUTF8"] = "1"
    env["PYTHONPATH"] = str(memory_root)

    params = StdioServerParameters(
        command=str(python),
        args=["-m", "servers.memory_server", "--root", str(project_root)],
        env=env,
    )

    async with stdio_client(params) as (read_stream, write_stream):
        async with ClientSession(read_stream, write_stream) as session:
            await session.initialize()
            for item in manifest["writes"]:
                result = await session.call_tool(
                    "memory_write",
                    arguments={
                        "path": item["path"],
                        "content": item["content"],
                        "mode": item.get("mode", "overwrite"),
                        "backup": True,
                        "create_if_missing": True,
                        "reason": manifest.get("reason", "batch write"),
                    },
                )
                print(f"{item['path']}: error={result.isError}")
                if result.isError:
                    raise RuntimeError(str(result.content))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--project-root", type=Path, required=True)
    args = parser.parse_args()
    asyncio.run(run(args.project_root, args.manifest))


if __name__ == "__main__":
    main()
