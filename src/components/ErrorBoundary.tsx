"use client";

import { Component, type ReactNode } from "react";
import { support } from "@/lib/ipc";

/**
 * Last line of defence for rendering bugs: the error is written to the log and the user
 * gets a way back. The design itself is safe in the autosave file, which is offered for
 * recovery on reload.
 */
export default class ErrorBoundary extends Component<{ children: ReactNode }, { error: Error | null }> {
  state = { error: null as Error | null };

  static getDerivedStateFromError(error: Error) {
    return { error };
  }

  componentDidCatch(error: Error, info: { componentStack?: string | null }) {
    void support.log("error", `UI crash: ${error.message} ${info.componentStack ?? ""}`).catch(() => undefined);
  }

  render() {
    if (!this.state.error) return this.props.children;
    return (
      <div className="crash">
        <div className="glass-strong crash-card">
          <h2>Datum Core hit a problem</h2>
          <p className="note">
            The interface stopped because of an unexpected error. Your design was autosaved and will be offered for recovery when the
            workspace reloads.
          </p>
          <pre className="mono">{this.state.error.message}</pre>
          <div className="row-actions">
            <button className="btn primary" onClick={() => window.location.reload()}>Reload workspace</button>
            <button className="btn" onClick={() => void support.openLogs().catch(() => undefined)}>Open log folder</button>
          </div>
        </div>
      </div>
    );
  }
}
