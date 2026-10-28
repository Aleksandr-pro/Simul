// ============================================================================
// @simul/renderer/wave-chart — High-DPI Canvas2D рендер волн алгоритма и частиц
// ============================================================================

import type { VideoWavePoint } from '@simul/core';

interface SparkParticle {
  x: number;
  y: number;
  vx: number;
  vy: number;
  life: number;
  maxLife: number;
  color: string;
  size: number;
}

const WAVE_COLORS: readonly [string, string, string, string, string] = [
  '#64748b', // Wave 0 — Тестовая группа
  '#38bdf8', // Wave 1 — Ниша
  '#818cf8', // Wave 2 — Широкая лента
  '#f59e0b', // Wave 3 — В тренде!
  '#ec4899', // Wave 4 — Мега-вирал!
];

export class WaveTelemetryCanvas {
  private readonly canvas: HTMLCanvasElement;
  private readonly ctx: CanvasRenderingContext2D;
  private points: VideoWavePoint[] = [];
  private retentionCurve: number[] = [];
  private particles: SparkParticle[] = [];
  private mode: 'views' | 'retention' = 'views';

  constructor(canvas: HTMLCanvasElement) {
    this.canvas = canvas;
    const context = canvas.getContext('2d');
    if (!context) {
      throw new Error('2D Canvas context unavailable');
    }
    this.ctx = context;
  }

  setMode(mode: 'views' | 'retention'): void {
    this.mode = mode;
    this.render(0.016);
  }

  setData(points: readonly VideoWavePoint[], retentionCurve: readonly number[]): void {
    const prevWave =
      this.points.length > 0 ? this.points[this.points.length - 1]!.waveIndex : 0;
    this.points = points.slice();
    this.retentionCurve = retentionCurve.slice();

    const latest = this.points[this.points.length - 1];
    if (latest && latest.waveIndex > prevWave && latest.waveIndex >= 2) {
      this.spawnWaveBurst(latest.waveIndex);
    }
  }

  spawnWaveBurst(waveIndex: 0 | 1 | 2 | 3 | 4): void {
    const rect = this.canvas.getBoundingClientRect();
    const cx = rect.width * 0.78;
    const cy = rect.height * 0.32;
    const color = WAVE_COLORS[waveIndex] ?? '#f59e0b';
    const count = waveIndex >= 4 ? 36 : 20;

    for (let i = 0; i < count; i++) {
      const angle = (Math.PI * 2 * i) / count + Math.random() * 0.35;
      const speed = 45 + Math.random() * 130;
      this.particles.push({
        x: cx,
        y: cy,
        vx: Math.cos(angle) * speed,
        vy: Math.sin(angle) * speed - 25,
        life: 0,
        maxLife: 0.65 + Math.random() * 0.45,
        color,
        size: 2.2 + Math.random() * 2.8,
      });
    }
  }

  get hasActiveParticles(): boolean {
    return this.particles.length > 0;
  }

  render(dtSeconds = 0.016): void {
    const dpr = typeof window !== 'undefined' ? window.devicePixelRatio || 1 : 1;
    const width = Math.max(260, this.canvas.clientWidth || 640);
    const height = Math.max(180, this.canvas.clientHeight || 260);

    if (
      this.canvas.width !== Math.round(width * dpr) ||
      this.canvas.height !== Math.round(height * dpr)
    ) {
      this.canvas.width = Math.round(width * dpr);
      this.canvas.height = Math.round(height * dpr);
    }

    const ctx = this.ctx;
    ctx.save();
    ctx.scale(dpr, dpr);

    // Фон карточки аналитики
    ctx.fillStyle = '#0b1020';
    ctx.fillRect(0, 0, width, height);

    const padLeft = 52;
    const padRight = 20;
    const padTop = 24;
    const padBottom = 32;
    const plotW = width - padLeft - padRight;
    const plotH = height - padTop - padBottom;

    // Сетка
    ctx.strokeStyle = 'rgba(148, 163, 184, 0.12)';
    ctx.lineWidth = 1;
    for (let i = 0; i <= 4; i++) {
      const y = padTop + (plotH * i) / 4;
      ctx.beginPath();
      ctx.moveTo(padLeft, y);
      ctx.lineTo(width - padRight, y);
      ctx.stroke();
    }

    if (this.mode === 'retention') {
      this.drawRetentionCurve(ctx, padLeft, padTop, plotW, plotH);
    } else {
      this.drawWaveTimeline(ctx, padLeft, padTop, plotW, plotH);
    }

    this.stepAndDrawParticles(ctx, dtSeconds);
    ctx.restore();
  }

  private drawWaveTimeline(
    ctx: CanvasRenderingContext2D,
    padLeft: number,
    padTop: number,
    plotW: number,
    plotH: number
  ): void {
    if (this.points.length === 0) {
      ctx.fillStyle = '#64748b';
      ctx.font = '13px system-ui, -apple-system, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(
        'Опубликуйте видео, чтобы увидеть живые волны показов (48 часов)',
        padLeft + plotW / 2,
        padTop + plotH / 2
      );
      return;
    }

    let maxViews = 50;
    for (const p of this.points) {
      if (p.views > maxViews) maxViews = p.views;
    }
    maxViews *= 1.15;

    // Подписи оси Y
    ctx.fillStyle = '#94a3b8';
    ctx.font = '11px ui-monospace, SFMono-Regular, monospace';
    ctx.textAlign = 'right';
    for (let i = 0; i <= 4; i++) {
      const val = Math.round(maxViews * (1 - i / 4));
      const y = padTop + (plotH * i) / 4 + 4;
      ctx.fillText(formatCompactNumber(val), padLeft - 8, y);
    }

    // Подписи оси X (часы 1..48)
    ctx.textAlign = 'center';
    for (const hr of [1, 12, 24, 36, 48]) {
      const x = padLeft + ((hr - 1) / 47) * plotW;
      ctx.fillText(`${hr}ч`, x, padTop + plotH + 20);
    }

    const latestWave = this.points[this.points.length - 1]!.waveIndex;
    const strokeColor = WAVE_COLORS[latestWave] ?? '#38bdf8';

    // Заливка под кривой
    const grad = ctx.createLinearGradient(0, padTop, 0, padTop + plotH);
    grad.addColorStop(0, hexWithAlpha(strokeColor, 0.36));
    grad.addColorStop(1, hexWithAlpha(strokeColor, 0.01));

    ctx.beginPath();
    for (let i = 0; i < this.points.length; i++) {
      const pt = this.points[i]!;
      const x = padLeft + ((pt.hourAfterPublish - 1) / 47) * plotW;
      const y = padTop + plotH - (pt.views / maxViews) * plotH;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    const lastPt = this.points[this.points.length - 1]!;
    const lastX = padLeft + ((lastPt.hourAfterPublish - 1) / 47) * plotW;
    const lastY = padTop + plotH - (lastPt.views / maxViews) * plotH;

    ctx.lineTo(lastX, padTop + plotH);
    ctx.lineTo(padLeft, padTop + plotH);
    ctx.closePath();
    ctx.fillStyle = grad;
    ctx.fill();

    // Главная линия почасовых просмотров
    ctx.beginPath();
    for (let i = 0; i < this.points.length; i++) {
      const pt = this.points[i]!;
      const x = padLeft + ((pt.hourAfterPublish - 1) / 47) * plotW;
      const y = padTop + plotH - (pt.views / maxViews) * plotH;
      if (i === 0) ctx.moveTo(x, y);
      else ctx.lineTo(x, y);
    }
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 2.6;
    ctx.lineJoin = 'round';
    ctx.stroke();

    // Пульсирующая головка текущего часа
    ctx.beginPath();
    ctx.arc(lastX, lastY, 4.5, 0, Math.PI * 2);
    ctx.fillStyle = '#ffffff';
    ctx.fill();
    ctx.lineWidth = 2.5;
    ctx.strokeStyle = strokeColor;
    ctx.stroke();

    // Плашка активной волны
    ctx.fillStyle = strokeColor;
    ctx.font = 'bold 12px system-ui, -apple-system, sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText(
      `Волна ${latestWave} • ViralScore ${lastPt.viralScore.toFixed(2)} • Обложка ${lastPt.activeVariant}`,
      padLeft + 6,
      padTop - 7
    );
  }

  private drawRetentionCurve(
    ctx: CanvasRenderingContext2D,
    padLeft: number,
    padTop: number,
    plotW: number,
    plotH: number
  ): void {
    const curve = this.retentionCurve;
    if (curve.length === 0) {
      ctx.fillStyle = '#64748b';
      ctx.font = '13px system-ui, -apple-system, sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(
        'Кривая удержания появится после первых просмотров',
        padLeft + plotW / 2,
        padTop + plotH / 2
      );
      return;
    }

    ctx.fillStyle = '#94a3b8';
    ctx.font = '11px ui-monospace, SFMono-Regular, monospace';
    ctx.textAlign = 'right';
    for (let i = 0; i <= 4; i++) {
      const pct = 100 - i * 25;
      ctx.fillText(`${pct}%`, padLeft - 8, padTop + (plotH * i) / 4 + 4);
    }

    const barGap = 8;
    const barW = (plotW - barGap * (curve.length - 1)) / curve.length;

    for (let i = 0; i < curve.length; i++) {
      const val = curve[i] ?? 0;
      const barH = val * plotH;
      const x = padLeft + i * (barW + barGap);
      const y = padTop + plotH - barH;

      ctx.fillStyle =
        val >= 0.55 ? '#22c55e' : val >= 0.35 ? '#38bdf8' : '#f43f5e';
      ctx.fillRect(x, y, barW, barH);

      ctx.fillStyle = '#94a3b8';
      ctx.textAlign = 'center';
      ctx.fillText(`${(i + 1) * 10}%`, x + barW / 2, padTop + plotH + 18);
    }

    ctx.fillStyle = '#e2e8f0';
    ctx.font = 'bold 12px system-ui, -apple-system, sans-serif';
    ctx.textAlign = 'left';
    ctx.fillText(
      'Удержание аудитории по децилям хронометража (10% — 100%)',
      padLeft + 4,
      padTop - 7
    );
  }

  private stepAndDrawParticles(ctx: CanvasRenderingContext2D, dt: number): void {
    if (this.particles.length === 0) return;
    const alive: SparkParticle[] = [];
    for (const p of this.particles) {
      p.life += dt;
      if (p.life >= p.maxLife) continue;
      p.vy += 180 * dt;
      p.x += p.vx * dt;
      p.y += p.vy * dt;
      const alpha = 1 - p.life / p.maxLife;
      ctx.fillStyle = hexWithAlpha(p.color, alpha);
      ctx.beginPath();
      ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
      ctx.fill();
      alive.push(p);
    }
    this.particles = alive;
  }
}

function hexWithAlpha(hex: string, alpha: number): string {
  const clean = hex.replace('#', '');
  const r = parseInt(clean.slice(0, 2), 16);
  const g = parseInt(clean.slice(2, 4), 16);
  const b = parseInt(clean.slice(4, 6), 16);
  return `rgba(${r}, ${g}, ${b}, ${alpha.toFixed(3)})`;
}

export function formatCompactNumber(value: number): string {
  const abs = Math.abs(value);
  if (abs >= 1_000_000) return `${(value / 1_000_000).toFixed(2)}M`;
  if (abs >= 1_000) return `${(value / 1_000).toFixed(1)}K`;
  return `${Math.round(value)}`;
}
