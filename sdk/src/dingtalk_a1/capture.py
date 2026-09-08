"""Live capture consumes device events; AI processing belongs in a separate worker."""
import json
from pathlib import Path
import time

from .audio import OggWriter
from .protocol import audio_push, event_body, ut_records


class LiveRecorder:
    def __init__(self, directory, *, device_id):
        self.directory = Path(directory)
        self.device_id = str(device_id)
        self._capture = None
        self._output = None
        self._writer = None
        self._previous = None

    def _start(self, fid):
        self._capture = {"fid": int(fid), "device_id": self.device_id, "source": "live",
                         "recording_key": f"{self.device_id}:{fid}", "markers": [],
                         "attributes": {}, "incomplete_reasons": [], "block_count": 0,
                         "start_observed": False}
        self._previous = None

    def _ensure(self, fid):
        events = []
        if self._capture and self._capture["fid"] != int(fid):
            events.extend(self.close("new_recording_before_stop"))
        if self._capture is None:
            self._start(fid)
        return events

    def feed(self, frame):
        events = []
        if frame.command == 0x0117:
            fid, number, packets = audio_push(frame.payload)
            if not packets:
                return events
            events.extend(self._ensure(fid))
            signature = (number, frame.payload[28:])
            if signature == self._previous:
                return events
            if self._previous and number != self._previous[0] + 1:
                self._capture["incomplete_reasons"].append(f"block_sequence:{self._previous[0]}->{number}")
            self._previous = signature
            if self._writer is None:
                self.directory.mkdir(parents=True, exist_ok=True)
                name = f"a1-live-{fid}-{time.time_ns()}"
                self._capture["_part"] = self.directory / (name + ".ogg.part")
                attrs = str(self._capture["attributes"].get("attrs", "")).split("@")
                rate = int(attrs[2]) if len(attrs) > 2 and attrs[2].isdigit() else 32000
                self._output = self._capture["_part"].open("xb")
                self._writer = OggWriter(self._output, rate, fid)
            for packet in packets:
                self._writer.write(packet)
            self._capture["block_count"] += 1
            return events
        if frame.command == 0x000c:
            if frame.kind not in (0x13, 0x14):
                return events
            try:
                records = list(ut_records(frame.payload))
            except ValueError:
                return [{"event": "telemetry_unparsed", "command": frame.command,
                         "payload_bytes": len(frame.payload)}]
            for record in records:
                # Batch may be delayed; don't assert a recording identity from "current" alone.
                events.append({"event": "telemetry_marker" if record["is_marker"] else "telemetry",
                               "data": record})
            return events
        if frame.kind not in (0x13, 0x14) or frame.command not in (0x0100, 0x0102, 0x0116):
            return events
        body = event_body(frame)
        current_fid = self._capture["fid"] if self._capture else None
        fid = body.get("fid") or current_fid
        if frame.command == 0x0100:
            action = body.get("action")
            if action == "start" and fid is not None:
                events.extend(self._ensure(fid))
                self._capture["start_observed"] = True
                events.append({"event": "recording_started", "fid": int(fid)})
            elif action == "stop" and self._capture and (fid is None or int(fid) == current_fid):
                events.extend(self.close())
        elif frame.command == 0x0116 and fid is not None:
            events.extend(self._ensure(fid))
            self._capture["attributes"].update(body)
            events.append({"event": "stream_attributes", "fid": int(fid), "data": body})
        elif frame.command == 0x0102:
            events.append({"event": "marker", "fid": fid, "data": body})
            if self._capture and fid is not None and int(fid) == current_fid:
                self._capture["markers"].append(body)
        return events

    def close(self, reason=None):
        capture, writer, output = self._capture, self._writer, self._output
        self._capture = self._writer = self._output = self._previous = None
        if capture is None:
            return []
        if writer is None:
            return [{"event": "recording_empty", "fid": capture["fid"]}]
        try:
            writer.close()
        finally:
            output.close()
        if not writer.packet_count:
            return [{"event": "recording_empty", "fid": capture["fid"],
                     "reason": reason, "partial_path": str(capture["_part"])}]
        if reason:
            capture["incomplete_reasons"].append(reason)
        if not capture["start_observed"]:
            capture["incomplete_reasons"].append("recording_start_not_observed")
        complete = not capture["incomplete_reasons"]
        part = capture.pop("_part")
        target = part.with_suffix("")
        if not complete:
            target = target.with_name(target.stem + "-incomplete.ogg")
        part.rename(target)
        attrs = capture["attributes"]
        capture.update({"path": str(target.resolve()), "complete": complete,
                        "stream_type": attrs.get("stream_type"), "kind": "live_audio",
                        "packet_count": writer.packet_count, "duration_seconds": writer.duration,
                        "full_decode_verified": False,
                        "stereo_coded_packet_count": writer.stereo_packet_count,
                        "non_20ms_packet_count": writer.non_20ms_packet_count})
        # stream_type is preserved; do not label all live streams as voice memos.
        with target.with_suffix(".json").open("x", encoding="utf-8") as meta:
            json.dump(capture, meta, ensure_ascii=False, indent=2)
        return [{"event": "recording_saved", **capture}]
