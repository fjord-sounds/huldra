// Composes the product image at the top of the README (docs/screenshots/hero-light.webp / hero-dark.webp):
// two Huldra windows in perspective over a forest lake at night, with veils of light in the sky,
// the logo and the key facts.
//
//   tools/screenshots/shoot.sh        # makes the raw page shots first, then calls this script
//   node hero.mjs                     # only the composition (raw shots in build/screenshots/raw)
//
// Requires Playwright (npm install in this folder) and cwebp.

import { chromium } from "playwright";
import { execFileSync } from "node:child_process";
import { mkdirSync, readdirSync, statSync, writeFileSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), "../..");
const RAW_DIR = join(ROOT, "build/screenshots/raw");
const PNG_DIR = join(ROOT, "build/screenshots/png");
const OUT_DIR = join(ROOT, "docs/screenshots");
const FONTS = join(ROOT, "resources/fonts");

// Colours from Source/ui/Theme.h (the veil: rose, lilac, ice blue)
const PALETTE = {
  light: { sky1: "#dfe5ee", sky2: "#f1f4f6", veil: ["#d98a8f", "#9d86d6", "#6fb1d6"], veilAlpha: 0.5,
           tree1: "#d3d9e3", tree2: "#bcc5d3", water: "#dde4ea", text: "#15253a", dim: "#5f7383", accent: "#b0545a",
           star: "rgba(40,60,90,0)", edge: "rgba(0,0,0,0.08)", shadow: "rgba(15,40,60,0.30)" },
  dark:  { sky1: "#070a14", sky2: "#161c33", veil: ["#f29c9c", "#b49cf2", "#7fc4e8"], veilAlpha: 0.42,
           tree1: "#141a2c", tree2: "#0a0e18", water: "#070a12", text: "#e2edf0", dim: "#8a9bb0", accent: "#f29c9c",
           star: "rgba(255,255,255,0.75)", edge: "rgba(255,255,255,0.08)", shadow: "rgba(0,0,0,0.65)" },
};

function presetCount() {
  let n = 0;
  const walk = (dir) => {
    for (const f of readdirSync(dir)) {
      const p = join(dir, f);
      if (statSync(p).isDirectory()) walk(p);
      else if (f.endsWith(".huldra")) n++;
    }
  };
  walk(join(ROOT, "presets/Factory"));
  return n;
}

const img = (id, theme) => pathToFileURL(join(RAW_DIR, `${id}-${theme}.png`)).href;

// Deterministic pseudo-random numbers (the image is reproducible)
function rng(seed) {
  let s = seed >>> 0;
  return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296);
}

// Three ribbons of light across the sky
function veils(c) {
  const ribbon = (k) => {
    const pts = [];
    for (let i = 0; i <= 80; i++) {
      const x = i / 80;
      const y = 250 + 55 * k + 70 * Math.sin(x * (3.0 + 0.6 * k) + k * 1.4) + 20 * Math.sin(x * 11 + k);
      pts.push(`${(x * 1600).toFixed(1)},${y.toFixed(1)}`);
    }
    return pts.join(" ");
  };
  return `<svg class="layer" viewBox="0 0 1600 900" preserveAspectRatio="none">
    <defs><filter id="soft" filterUnits="userSpaceOnUse" x="-100" y="-100" width="1800" height="1100"><feGaussianBlur stdDeviation="22"/></filter><filter id="softer" filterUnits="userSpaceOnUse" x="-100" y="-100" width="1800" height="1100"><feGaussianBlur stdDeviation="6"/></filter></defs>
    ${[0, 1, 2].map((k) => `
      <polyline points="${ribbon(k)}" fill="none" stroke="${c.veil[k]}" stroke-width="150" opacity="${c.veilAlpha * 0.5}" filter="url(#soft)"/>
      <polyline points="${ribbon(k)}" fill="none" stroke="${c.veil[k]}" stroke-width="26" opacity="${c.veilAlpha}" filter="url(#softer)"/>
      <polyline points="${ribbon(k)}" fill="none" stroke="${c.veil[k]}" stroke-width="2.5" opacity="${Math.min(1, c.veilAlpha * 2)}"/>`).join("")}
  </svg>`;
}

function stars(c) {
  const r = rng(7);
  let out = "";
  for (let i = 0; i < 120; i++) {
    const s = 0.8 + r() * 1.8;
    out += `<circle cx="${(r() * 1600).toFixed(1)}" cy="${(r() * 420).toFixed(1)}" r="${s.toFixed(2)}" fill="${c.star}" opacity="${(0.3 + r() * 0.7).toFixed(2)}"/>`;
  }
  return `<svg class="layer" viewBox="0 0 1600 900" preserveAspectRatio="none">${out}</svg>`;
}

// Two rows of pointed trees and the still lake with the veil reflected in it
function forest(c) {
  const row = (base, height, fill, seed) => {
    const r = rng(seed);
    let x = 0, pts = `0,900 0,${base}`;
    while (x < 1600) {
      const w = 30 + r() * 46, h = height * (0.55 + 0.7 * r());
      pts += ` ${x.toFixed(1)},${base} ${(x + w / 2).toFixed(1)},${(base - h).toFixed(1)} ${(x + w).toFixed(1)},${base}`;
      x += w * 0.78;
    }
    return `<polygon points="${pts} 1600,${base} 1600,900" fill="${fill}"/>`;
  };
  const lake = 760;
  return `<svg class="layer" viewBox="0 0 1600 900" preserveAspectRatio="none">
    ${row(745, 120, c.tree1, 3)}
    ${row(762, 80, c.tree2, 11)}
    <rect x="0" y="${lake}" width="1600" height="${900 - lake}" fill="${c.water}"/>
    <rect x="140" y="${lake + 26}" width="460" height="3" rx="1.5" fill="${c.veil[0]}" opacity="0.6"/>
    <rect x="230" y="${lake + 52}" width="290" height="3" rx="1.5" fill="${c.veil[1]}" opacity="0.5"/>
    <rect x="300" y="${lake + 78}" width="150" height="3" rx="1.5" fill="${c.veil[2]}" opacity="0.55"/>
    <rect x="340" y="${lake + 104}" width="60" height="3" rx="1.5" fill="${c.veil[0]}" opacity="0.5"/>
  </svg>`;
}

function hero(theme) {
  const c = PALETTE[theme];
  const other = theme === "light" ? "dark" : "light";
  return `<!doctype html><meta charset="utf-8"><style>
  @font-face { font-family: Manrope; font-weight: 400; src: url(${pathToFileURL(join(FONTS, "Manrope-Regular.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 500; src: url(${pathToFileURL(join(FONTS, "Manrope-Medium.ttf")).href}); }
  @font-face { font-family: Manrope; font-weight: 600; src: url(${pathToFileURL(join(FONTS, "Manrope-SemiBold.ttf")).href}); }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  html, body { width: 100%; height: 100%; }
  body { font-family: Manrope, sans-serif; color: ${c.text}; overflow: hidden; position: relative;
         background: linear-gradient(180deg, ${c.sky1}, ${c.sky2} 70%); }
  .layer { position: absolute; inset: 0; width: 100%; height: 100%; }
  .copy { position: absolute; left: 96px; top: 210px; width: 520px; z-index: 2; }
  .logo { font-size: 104px; font-weight: 500; letter-spacing: -3px; line-height: 1; }
  .logo span { color: ${c.accent}; }
  .tag { margin-top: 26px; font-size: 27px; line-height: 1.35; font-weight: 500; }
  .facts { margin-top: 30px; font-size: 15px; letter-spacing: 2.2px; text-transform: uppercase; color: ${c.dim};
           line-height: 2.1; font-weight: 600; }
  .facts b { color: ${c.accent}; font-weight: 600; }
  .scene { position: absolute; left: 620px; top: 110px; perspective: 2600px; z-index: 1; }
  .window { display: block; position: absolute; width: 1100px; border-radius: 14px; transform-origin: left center;
            box-shadow: 0 0 0 1px ${c.edge}, 0 50px 90px -30px ${c.shadow}, 0 18px 36px -18px ${c.shadow}; }
  .back  { top: -150px; left: 190px; transform: rotateY(-18deg) rotateX(4deg) scale(0.9); opacity: 0.97; }
  .front { top: 70px; left: 0; transform: rotateY(-18deg) rotateX(4deg); }
  </style>
  ${stars(c)}
  ${veils(c)}
  ${forest(c)}
  <div class="copy">
    <div class="logo">huldra<span>.</span></div>
    <div class="tag">Your voice, turned into instruments.<br>Record, chop, freeze, stretch.</div>
    <div class="facts">
      <b>4</b> engines · granular · spectral · formant<br>
      slices · keymap · every key editable<br>
      motion · <b>4</b> lfos · <b>3</b> msegs · <b>15</b> effects<br>
      <b>${presetCount()}</b> factory presets<br>
      <b>VST3</b> · <b>AU</b> · <b>LV2</b> · standalone
    </div>
  </div>
  <div class="scene">
    <img class="window back" src="${img("engine", other)}">
    <img class="window front" src="${img("source", theme)}">
  </div>`;
}

mkdirSync(OUT_DIR, { recursive: true });
mkdirSync(PNG_DIR, { recursive: true });
const browser = await chromium.launch();
const page = await browser.newPage({ deviceScaleFactor: 1.5 });
try {
  for (const theme of ["light", "dark"]) {
    const htmlFile = join(RAW_DIR, `_hero-${theme}.html`);
    writeFileSync(htmlFile, hero(theme));
    await page.setViewportSize({ width: 1600, height: 900 });
    await page.goto(pathToFileURL(htmlFile).href);
    await page.evaluate(() => document.fonts.ready);
    await page.waitForFunction(() => [...document.images].every((i) => i.complete && i.naturalWidth > 0));
    const png = join(PNG_DIR, `hero-${theme}.png`);
    const out = join(OUT_DIR, `hero-${theme}.webp`);
    await page.screenshot({ path: png });
    execFileSync("cwebp", ["-quiet", "-q", "88", "-m", "6", png, "-o", out]);
    console.log(`hero  ${out.replace(ROOT + "/", "")} (${Math.round(statSync(out).size / 1024)} KB)`);
  }
} finally {
  await browser.close();
}
