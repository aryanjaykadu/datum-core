import type { NextConfig } from "next";

const nextConfig: NextConfig = {
  // Datum Core Enterprise ships as a static Next bundle embedded in Tauri.
  // No server route or API surface is required at runtime; all CAD work is
  // performed by the Rust/Python sidecar.
  output: "export",
  images: { unoptimized: true },
  trailingSlash: true,
  assetPrefix: "./",
  devIndicators: false,
  typescript: { ignoreBuildErrors: false },
};

export default nextConfig;
