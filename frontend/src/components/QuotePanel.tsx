"use client";

import { useEffect, useState } from "react";
import { kernel, type Catalog, type Evaluation, type Quote } from "@/lib/ipc";

const money = (v: number) => v.toLocaleString("en-US", { style: "currency", currency: "USD" });

export default function QuotePanel({ result, catalog, material }: { result: Evaluation | null; catalog: Catalog | null; material: string }) {
  const [mat, setMat] = useState(material);
  const [qty, setQty] = useState(1);
  const [quote, setQuote] = useState<Quote | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => setMat(material), [material]);
  useEffect(() => {
    if (!result) return;
    let live = true;
    kernel.quote(mat, qty).then((q) => live && (setQuote(q), setError(null))).catch((e) => live && setError(String(e)));
    return () => {
      live = false;
    };
  }, [result, mat, qty]);

  if (!result) return null;
  return (
    <div className="section mono">
      <h3>Manufacturing quote · CNC</h3>
      <div className="quote-controls">
        <select value={mat} onChange={(e) => setMat(e.target.value)}>
          {catalog?.materials.map((m) => <option key={m.key} value={m.key}>{m.name}</option>)}
        </select>
        <input type="number" min={1} max={10000} value={qty} onChange={(e) => setQty(Math.max(1, Math.min(10000, Number(e.target.value) || 1)))} />
      </div>
      {error && <div className="note warn">{error}</div>}
      {quote && (
        <>
          <div className="price">{money(quote.price_usd)}</div>
          <div className="note">{money(quote.unit_price_usd)} each · {quote.material} · {quote.lead_days} days</div>
          <div className="kv"><span>Setups</span><span>{quote.setups}</span></div>
          <div className="kv"><span>Machining</span><span>{quote.machining_h.toFixed(2)} h / part</span></div>
          <div className="kv"><span>Stock</span><span>{quote.stock_kg.toFixed(3)} kg / part</span></div>
          <div className="kv"><span>Embodied carbon</span><span>{quote.carbon_kg.toFixed(2)} kg CO₂e</span></div>
          <div className="kv"><span>Platform fee ({(quote.fee_rate * 100).toFixed(1)}%)</span><span>{money(quote.fee_usd)}</span></div>
          <button className="btn primary wide" disabled title="Connect a manufacturing partner account in Settings to place orders">
            Order via Datum
          </button>
          <div className="note">
            Estimate from geometry ({quote.provider}): stock, roughing time and setup count. Live partner pricing and ordering need a
            manufacturing partner agreement and API key.
          </div>
        </>
      )}
    </div>
  );
}
