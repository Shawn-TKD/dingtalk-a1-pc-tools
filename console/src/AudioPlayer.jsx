import { useEffect, useMemo, useRef, useState } from "react";
import { PauseIcon, PlayIcon } from "./icons";

function formatTime(value) {
  if (!Number.isFinite(value)) return "00:00";
  const seconds = Math.max(0, Math.floor(value));
  return `${String(Math.floor(seconds / 60)).padStart(2, "0")}:${String(seconds % 60).padStart(2, "0")}`;
}

export default function AudioPlayer({ src, fallbackDuration = 0 }) {
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

  const ratio = duration ? currentTime / duration : 0;

  return (
    <div className="audio-player">
      <audio
        ref={audioRef}
        src={src}
        preload="metadata"
        onPlay={() => setPlaying(true)}
        onPause={() => setPlaying(false)}
        onEnded={() => setPlaying(false)}
        onTimeUpdate={(event) => setCurrentTime(event.currentTarget.currentTime)}
        onLoadedMetadata={(event) => setDuration(event.currentTarget.duration)}
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
          if (audioRef.current && duration) audioRef.current.currentTime = nextRatio * duration;
        }}
      >
        {bars.map((height, index) => (
          <span key={index} className={index / bars.length <= ratio ? "played" : ""} style={{ height }} />
        ))}
      </button>
      <span className="audio-time">{formatTime(currentTime)} / {formatTime(duration)}</span>
    </div>
  );
}
