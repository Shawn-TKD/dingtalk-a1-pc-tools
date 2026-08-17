import { useEffect, useMemo, useRef, useState } from "react";
import { PauseIcon, PlayIcon } from "./icons";

function formatTime(value) {
  if (!Number.isFinite(value)) return "00:00";
  const seconds = Math.max(0, Math.floor(value));
  return `${String(Math.floor(seconds / 60)).padStart(2, "0")}:${String(seconds % 60).padStart(2, "0")}`;
}

export default function AudioPlayer({ src, fallbackDuration = 0, large = false, markers = [] }) {
  const audioRef = useRef(null);
  const [playing, setPlaying] = useState(false);
  const [currentTime, setCurrentTime] = useState(0);
  const [duration, setDuration] = useState(fallbackDuration);
  const bars = useMemo(() => [8, 13, 19, 11, 22, 16, 25, 13, 20, 27, 16, 11, 22, 17, 9, 14, 7, 12, 8, 10], []);

  useEffect(() => {
    setPlaying(false);
    setCurrentTime(0);
    setDuration(fallbackDuration);
  }, [src, fallbackDuration]);

  function toggle() {
    const audio = audioRef.current;
    if (!audio) return;
    if (audio.paused) audio.play();
    else audio.pause();
  }

  function updateDuration(event) {
    const nextDuration = event.currentTarget.duration;
    if (Number.isFinite(nextDuration) && nextDuration > 0) setDuration(nextDuration);
  }

  const ratio = duration ? currentTime / duration : 0;

  function seek(seconds) {
    if (!audioRef.current || !duration) return;
    audioRef.current.currentTime = Math.min(Math.max(Number(seconds) || 0, 0), duration);
  }

  return (
    <div className={large ? "audio-player-stack large" : "audio-player-stack"}>
    <div className={large ? "audio-player large" : "audio-player"}>
      <audio
        ref={audioRef}
        src={src}
        preload="metadata"
        onPlay={() => setPlaying(true)}
        onPause={() => setPlaying(false)}
        onEnded={() => setPlaying(false)}
        onTimeUpdate={(event) => setCurrentTime(event.currentTarget.currentTime)}
        onLoadedMetadata={updateDuration}
        onDurationChange={updateDuration}
      />
      <button className="play-control" type="button" onClick={toggle} aria-label={playing ? "暂停" : "播放"}>
        {playing ? <PauseIcon size={16} /> : <PlayIcon size={16} filled />}
      </button>
      <button
        className="waveform"
        type="button"
        aria-label="跳转音频位置"
        onClick={(event) => {
          const rect = event.currentTarget.getBoundingClientRect();
          const nextRatio = (event.clientX - rect.left) / rect.width;
          seek(nextRatio * duration);
        }}
      >
        {bars.map((height, index) => (
          <span key={index} className={index / bars.length <= ratio ? "played" : ""} style={{ height }} />
        ))}
        {markers.map((marker, index) => (
          <i
            className="waveform-marker"
            key={`${marker.relative_seconds}-${index}`}
            style={{ left: `${Math.min(100, Math.max(0, (Number(marker.relative_seconds) / (duration || 1)) * 100))}%` }}
          />
        ))}
      </button>
      <span className="audio-time">{formatTime(currentTime)} / {formatTime(duration)}</span>
    </div>
    {large && markers.length > 0 && <div className="audio-markers"><span>录音标记</span>{markers.map((marker, index) => <button type="button" key={`${marker.relative_seconds}-${index}`} onClick={() => seek(marker.relative_seconds)}>标记 {index + 1} · {formatTime(Number(marker.relative_seconds))}</button>)}</div>}
    </div>
  );
}
