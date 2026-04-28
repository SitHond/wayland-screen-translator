#!/usr/bin/env python3
from __future__ import annotations

import asyncio
from pathlib import Path
from urllib.parse import unquote, urlparse
import sys
import uuid


def main() -> int:
    try:
        path = asyncio.run(capture_screenshot())
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        return 1
    print(path)
    return 0


async def capture_screenshot() -> str:
    from dbus_next import Message, MessageType, Variant
    from dbus_next.aio import MessageBus

    bus = await MessageBus().connect()
    loop = asyncio.get_running_loop()
    future: asyncio.Future[dict[str, object]] = loop.create_future()
    token = f"screen_translator_{uuid.uuid4().hex}"
    handle_path = ""

    def on_message(message: Message) -> bool:
        if (
            message.message_type == MessageType.SIGNAL
            and message.interface == "org.freedesktop.portal.Request"
            and message.member == "Response"
            and message.path == handle_path
        ):
            response_code = message.body[0]
            results = message.body[1]
            if response_code != 0:
                if not future.done():
                    future.set_exception(
                        RuntimeError(
                            f"portal screenshot denied or cancelled (response {response_code})"
                        )
                    )
            elif not future.done():
                future.set_result(results)
            return True
        return False

    request = Message(
        destination="org.freedesktop.portal.Desktop",
        path="/org/freedesktop/portal/desktop",
        interface="org.freedesktop.portal.Screenshot",
        member="Screenshot",
        signature="sa{sv}",
        body=[
            "",
            {
                "handle_token": Variant("s", token),
                "interactive": Variant("b", False),
                "modal": Variant("b", False),
            },
        ],
    )
    reply = await bus.call(request)
    if reply.message_type == MessageType.ERROR:
        raise RuntimeError(reply.body[0] if reply.body else reply.error_name)
    handle_path = reply.body[0]

    bus.add_message_handler(on_message)
    try:
        results = await asyncio.wait_for(future, timeout=20)
    finally:
        bus.remove_message_handler(on_message)
        bus.disconnect()

    uri_variant = results.get("uri")
    uri = getattr(uri_variant, "value", uri_variant)
    if not uri:
        raise RuntimeError("portal screenshot did not return an URI")

    parsed = urlparse(uri)
    return str(Path(unquote(parsed.path)))


if __name__ == "__main__":
    raise SystemExit(main())
