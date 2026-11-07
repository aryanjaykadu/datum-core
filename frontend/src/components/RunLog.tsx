"use client";

import { useEffect, useRef } from "react";

export interface LogLine {
  id: number;
  t: string;
  level: "info" | "warn" | "error";
  stage: string;
  msg: string;
}

export default function RunLog({ lines, progress }: { lines: LogLine[]; progress: number | null }) {
  const end = useRef<HTMLDivElement>(null);
  useEffect(() => {
    // braces matter: newer Chromium returns a Promise from scrollIntoView, which React would treat as a cleanup
    end.current?.scrollIntoView({ block: "end" });
  }, [lines.length]);
  return (
    <>
      <div className="progress"><i style={{ width: `${(progress ?? 0) * 100}%`, opacity: progress === null ? 0 : 1 }} /></div>
      <div className="log mono scroll" style={{ height: "calc(100% - 52px)" }}>
        {lines.length === 0 && <div className="t">Waiting for the engine…</div>}
        {lines.map((l) => (
          <div key={l.id} className={`row ${l.level}`}>
            <span className="t">{l.t}</span>
            <span className="stage">{l.stage}</span>
            <span className="msg" title={l.msg}>{l.msg}</span>
          </div>
        ))}
        <div ref={end} />
      </div>
    </>
  );
}
