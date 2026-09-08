"""A long connection while another coroutine can still query device status.

Run: python examples/recording_to_agent.py /private/path/.a1-device.json
Insert your ASR/Agent adapter in the consumer. No vendor key is embedded.
"""
import asyncio
import json
from pathlib import Path
import sys

from dingtalk_a1 import Identity, A1Client, LiveRecorder
from dingtalk_a1.agent import ModeRouter


async def main(identity_path):
    identity = Identity.load(identity_path)
    router = ModeRouter()
    finished = asyncio.Queue()

    async def ai_worker():
        while True:
            event = await finished.get()
            print(json.dumps(event, ensure_ascii=False), flush=True)
            # Replace these comments with your existing ASR and Agent adapter:
            # transcript = await your_asr.transcribe(Path(event['path']))
            # task = router.route(transcript, recording_key=event['recording_key'])
            # await your_agent.submit(task)   # project/tool scope belongs to your app.
            # Never execute transcript with eval(), shell=True, or a raw terminal.
            # A1 BLE vibration has no verified command: notify on phone/PC for now.

    recorder = LiveRecorder(Path("recordings/live"), device_id=identity.serial_number or identity.did)
    async with A1Client(identity) as client:
        worker = asyncio.create_task(ai_worker())
        try:
            async with client.subscribe() as queue:
                print("READY: use your A1 voice/record key", file=sys.stderr)
                while True:
                    for event in recorder.feed(await client.next_frame(queue)):
                        if event["event"] == "recording_saved" and event["complete"]:
                            finished.put_nowait(event)
        finally:
            for event in recorder.close("listener_ended"):
                print(json.dumps(event, ensure_ascii=False))
            worker.cancel()
            await asyncio.gather(worker, return_exceptions=True)


if __name__ == "__main__":
    asyncio.run(main(sys.argv[1]))
