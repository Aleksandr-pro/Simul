// ============================================================================
// @simul/core/math/curves — Математические примитивы алгоритма и телеметрии
// ============================================================================

export function clamp(value: number, min: number, max: number): number {
  if (value < min) return min;
  if (value > max) return max;
  return value;
}

export function lerp(a: number, b: number, t: number): number {
  return a + (b - a) * clamp(t, 0, 1);
}

/** Логистическая сигмоида (0..1), устойчивая к большим аргументам */
export function sigmoid(x: number): number {
  if (x >= 20) return 1;
  if (x <= -20) return 0;
  return 1 / (1 + Math.exp(-x));
}

/** Плавный шаг Эрмита (0..1) */
export function smoothstep(edge0: number, edge1: number, x: number): number {
  if (edge1 === edge0) return x < edge0 ? 0 : 1;
  const t = clamp((x - edge0) / (edge1 - edge0), 0, 1);
  return t * t * (3 - 2 * t);
}

/** Экспоненциальное скользящее среднее */
export function ema(previous: number, sample: number, alpha: number): number {
  return previous + alpha * (sample - previous);
}

/**
 * Кольцевой буфер фиксированной ёмкости на Float64Array (0 аллокаций при записи)
 * Используется для хранения живой истории метрик в реальном времени.
 */
export class FloatRingBuffer {
  private readonly buffer: Float64Array;
  private head = 0;
  private lengthInternal = 0;

  constructor(public readonly capacity: number) {
    if (capacity < 1) {
      throw new Error('FloatRingBuffer capacity must be >= 1');
    }
    this.buffer = new Float64Array(capacity);
  }

  push(value: number): void {
    this.buffer[this.head] = value;
    this.head = (this.head + 1) % this.capacity;
    if (this.lengthInternal < this.capacity) {
      this.lengthInternal += 1;
    }
  }

  get length(): number {
    return this.lengthInternal;
  }

  toArray(): number[] {
    const out = new Array<number>(this.lengthInternal);
    const start =
      this.lengthInternal < this.capacity ? 0 : this.head;
    for (let i = 0; i < this.lengthInternal; i++) {
      out[i] = this.buffer[(start + i) % this.capacity] ?? 0;
    }
    return out;
  }

  clear(): void {
    this.head = 0;
    this.lengthInternal = 0;
  }
}
