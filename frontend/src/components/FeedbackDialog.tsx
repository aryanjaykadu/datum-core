"use client";

import { useState } from "react";
import { support } from "@/lib/ipc";
import Icon from "./Icon";

const REPO = "https://github.com/aryanjaykadu/datum-core";
const KINDS = [
  { key: "bug", label: "Something went wrong", icon: "close", title: "Bug: ", label_gh: "bug" },
  { key: "idea", label: "An idea", icon: "sparkle", title: "Idea: ", label_gh: "enhancement" },
  { key: "praise", label: "Something I liked", icon: "check", title: "Feedback: ", label_gh: "feedback" },
] as const;

/**
 * Feedback goes to the project's GitHub issues: the dialog opens a pre-filled issue in the
 * browser and the user reviews and submits it there. Nothing is sent from the app itself.
 */
export default function FeedbackDialog({ onClose, context }: { onClose: () => void; context: { version: string; gpu: string; part: string } }) {
  const [kind, setKind] = useState<(typeof KINDS)[number]["key"]>("bug");
  const [summary, setSummary] = useState("");
  const [details, setDetails] = useState("");
  const [withSystem, setWithSystem] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const k = KINDS.find((x) => x.key === kind)!;
  const body = [
    details.trim() || "(no details)",
    kind === "bug" ? "\n**What I expected instead:**\n\n" : "",
    withSystem ? `\n---\nDatum Core ${context.version} · Windows · GPU: ${context.gpu} · part: ${context.part}` : "",
  ].join("\n");
  const url = `${REPO}/issues/new?${new URLSearchParams({ title: k.title + summary.trim(), body, labels: k.label_gh }).toString()}`;
  const send = async () => {
    try {
      await support.openUrl(url);
      onClose();
    } catch (e) {
      setError(String(e));
    }
  };
  return (
    <div className="modal-backdrop" onPointerDown={onClose}>
      <div className="modal glass-strong feedback" role="dialog" aria-label="Send feedback" onPointerDown={(e) => e.stopPropagation()}>
        <div className="panel-title">Send feedback <button className="icon" onClick={onClose} aria-label="Close">×</button></div>
        <div className="modal-body">
          <div className="kind-row">
            {KINDS.map((x) => (
              <button key={x.key} aria-pressed={kind === x.key} onClick={() => setKind(x.key)}><Icon name={x.icon} size={16} />{x.label}</button>
            ))}
          </div>
          <label className="fb-field">
            <span>In one line</span>
            <input value={summary} placeholder={kind === "bug" ? "e.g. Make it lighter fails on my imported bracket" : kind === "idea" ? "e.g. Let me set the infill pattern" : "e.g. The stress colours made it click"} onChange={(e) => setSummary(e.target.value)} />
          </label>
          <label className="fb-field">
            <span>{kind === "bug" ? "What happened? Steps help a lot." : "Tell us more"}</span>
            <textarea rows={5} value={details} onChange={(e) => setDetails(e.target.value)} />
          </label>
          <label className="fb-check">
            <input type="checkbox" checked={withSystem} onChange={(e) => setWithSystem(e.target.checked)} />
            <span>Include app version, graphics card and part name ({context.version} · {context.gpu} · {context.part})</span>
          </label>
          <p className="note small">This opens a GitHub issue in your browser with your text filled in. You review it and press Submit there (a free GitHub account is needed). Nothing is sent from the app.</p>
          {error && <p className="note bad">{error}</p>}
        </div>
        <div className="modal-foot">
          <button className="btn" onClick={onClose}>Cancel</button>
          <button className="btn primary" disabled={!summary.trim()} onClick={() => void send()}><Icon name="play" size={14} />Continue on GitHub</button>
        </div>
      </div>
    </div>
  );
}
