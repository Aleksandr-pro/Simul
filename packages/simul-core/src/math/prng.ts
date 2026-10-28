// ============================================================================
// @simul/core/math/prng — Детерминированный ГПСЧ Xoshiro128** (бит-в-бит на всех JS VM)
// ============================================================================

export type PrngStateTuple = [number, number, number, number];

function splitMix32(seed: number): () => number {
  let state = seed >>> 0;
  return (): number => {
    state = (state + 0x9e3779b9) >>> 0;
    let z = state;
    z = Math.imul(z ^ (z >>> 16), 0x85ebca6b) >>> 0;
    z = Math.imul(z ^ (z >>> 13), 0xc2b2ae35) >>> 0;
    return (z ^ (z >>> 16)) >>> 0;
  };
}

function rotl32(x: number, k: number): number {
  return ((x << k) | (x >>> (32 - k))) >>> 0;
}

export class SeededPrng {
  private s0: number;
  private s1: number;
  private s2: number;
  private s3: number;

  constructor(seedOrState: number | PrngStateTuple = 20261029) {
    if (Array.isArray(seedOrState)) {
      this.s0 = seedOrState[0] >>> 0;
      this.s1 = seedOrState[1] >>> 0;
      this.s2 = seedOrState[2] >>> 0;
      this.s3 = seedOrState[3] >>> 0;
      if ((this.s0 | this.s1 | this.s2 | this.s3) === 0) {
        this.s0 = 1;
      }
    } else {
      const nextSeed = splitMix32(seedOrState >>> 0);
      this.s0 = nextSeed();
      this.s1 = nextSeed();
      this.s2 = nextSeed();
      this.s3 = nextSeed();
      if ((this.s0 | this.s1 | this.s2 | this.s3) === 0) {
        this.s0 = 0x6d2b79f5;
      }
    }
  }

  /** Возвращает целое беззнаковое 32-битное число [0, 2^32 - 1] */
  nextUint32(): number {
    const result = Math.imul(rotl32(Math.imul(this.s1, 5) >>> 0, 7), 9) >>> 0;
    const t = (this.s1 << 9) >>> 0;

    this.s2 = (this.s2 ^ this.s0) >>> 0;
    this.s3 = (this.s3 ^ this.s1) >>> 0;
    this.s1 = (this.s1 ^ this.s2) >>> 0;
    this.s0 = (this.s0 ^ this.s3) >>> 0;

    this.s2 = (this.s2 ^ t) >>> 0;
    this.s3 = rotl32(this.s3, 11);

    return result;
  }

  /** Равномерное число с плавающей точкой в полуинтервале [0, 1) */
  nextFloat(): number {
    return this.nextUint32() / 4294967296;
  }

  /** Число в диапазоне [min, max) */
  range(min: number, max: number): number {
    return min + (max - min) * this.nextFloat();
  }

  /** Целое число в диапазоне [min, max] включительно */
  intRange(min: number, max: number): number {
    if (max <= min) return min;
    const span = max - min + 1;
    return min + Math.floor(this.nextFloat() * span);
  }

  /** Приближённое нормальное распределение N(mean, stdDev) по методу Ирвина — Холла */
  gaussian(mean = 0, stdDev = 1): number {
    const sum =
      this.nextFloat() +
      this.nextFloat() +
      this.nextFloat() +
      this.nextFloat() +
      this.nextFloat() +
      this.nextFloat() -
      3.0;
    // Для 6 равномерных величин дисперсия суммы равна 6/12 = 0.5 -> std = sqrt(0.5) ≈ 0.70710678
    return mean + (sum * 1.4142135623730951) * stdDev;
  }

  /** Проверка вероятности p in [0..1] */
  chance(probability: number): boolean {
    if (probability <= 0) return false;
    if (probability >= 1) return true;
    return this.nextFloat() < probability;
  }

  /** Выбор случайного элемента непустого массива */
  pick<T>(items: readonly T[]): T {
    if (items.length === 0) {
      throw new Error('SeededPrng.pick called on empty array');
    }
    const idx = Math.floor(this.nextFloat() * items.length);
    return items[idx]!;
  }

  getState(): PrngStateTuple {
    return [this.s0, this.s1, this.s2, this.s3];
  }

  setState(state: PrngStateTuple): void {
    this.s0 = state[0] >>> 0;
    this.s1 = state[1] >>> 0;
    this.s2 = state[2] >>> 0;
    this.s3 = state[3] >>> 0;
  }
}
