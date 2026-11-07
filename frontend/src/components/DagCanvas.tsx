"use client";

import { Background, BackgroundVariant, Controls, ReactFlow, type Connection, type Edge, type Node } from "@xyflow/react";
import "@xyflow/react/dist/base.css";
import { useMemo } from "react";
import type { DatumDocument } from "@/lib/ipc";

/**
 * The parametric DAG as a node graph. Layout: each feature sits one column right of its
 * deepest input. Dragging a connection onto a feature adds it as an input.
 */
export default function DagCanvas({ doc, selected, onSelect, onConnect }: {
  doc: DatumDocument;
  selected: string | null;
  onSelect: (id: string) => void;
  onConnect: (source: string, target: string) => void;
}) {
  const { nodes, edges } = useMemo(() => {
    const depth = new Map<string, number>();
    const byId = new Map(doc.features.map((f) => [f.id, f]));
    const level = (id: string, seen = new Set<string>()): number => {
      if (depth.has(id)) return depth.get(id)!;
      if (seen.has(id)) return 0;
      seen.add(id);
      const f = byId.get(id);
      const d = f?.inputs?.length ? 1 + Math.max(...f.inputs.map((i) => level(i, seen))) : 0;
      depth.set(id, d);
      return d;
    };
    const rows = new Map<number, number>();
    const nodes: Node[] = doc.features.map((f) => {
      const d = level(f.id);
      const r = rows.get(d) ?? 0;
      rows.set(d, r + 1);
      return {
        id: f.id,
        position: { x: d * 190, y: r * 64 },
        data: { label: f.name ?? f.id },
        className: [f.id === selected ? "selected" : "", f.suppressed ? "suppressed" : "", f.id === doc.root ? "root" : ""].join(" "),
        sourcePosition: "right" as Node["sourcePosition"],
        targetPosition: "left" as Node["targetPosition"],
      };
    });
    const edges: Edge[] = doc.features.flatMap((f) => (f.inputs ?? []).map((i) => ({ id: `${i}->${f.id}`, source: i, target: f.id, animated: f.id === doc.root })));
    return { nodes, edges };
  }, [doc, selected]);

  return (
    <div className="rubric">
      <ReactFlow nodes={nodes} edges={edges} fitView colorMode="dark" proOptions={{ hideAttribution: true }}
        nodesDraggable={false} onNodeClick={(_, n) => onSelect(n.id)}
        onConnect={(c: Connection) => c.source && c.target && onConnect(c.source, c.target)}>
        <Background variant={BackgroundVariant.Dots} gap={18} size={1} color="#22252c" />
        <Controls showInteractive={false} position="bottom-right" />
      </ReactFlow>
    </div>
  );
}
