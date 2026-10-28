// ============================================================================
// @simul/renderer/springs — Пружинная физика 60/120 FPS для карточек и счётчиков
// ============================================================================

export interface SpringConfig {
  stiffness?: number;
  damping?: number;
  mass?: number;
  precision?: number;
}

export class SpringValue {
  public value: number;
  public target: number;
  public velocity = 0;

  private readonly stiffness: number;
  private readonly damping: number;
  private readonly mass: number;
  private readonly precision: number;

  constructor(initialValue = 0, config: SpringConfig = {}) {
    this.value = initialValue;
    this.target = initialValue;
    this.stiffness = config.stiffness ?? 210;
    this.damping = config.damping ?? 22;
    this.mass = config.mass ?? 1;
    this.precision = config.precision ?? 0.001;
  }

  setTarget(nextTarget: number): void {
    this.target = nextTarget;
  }

  snap(immediateValue: number): void {
    this.value = immediateValue;
    this.target = immediateValue;
    this.velocity = 0;
  }

  get isSettled(): boolean {
    return (
      Math.abs(this.target - this.value) < this.precision &&
      Math.abs(this.velocity) < this.precision
    );
  }

  /** Шаг интегрирования (dt в секундах, безопасно ограничен до 1/30с от скачков вкладки) */
  step(dtSeconds: number): number {
    if (this.isSettled) {
      this.value = this.target;
      this.velocity = 0;
      return this.value;
    }

    const dt = Math.min(0.033, Math.max(0.001, dtSeconds));
    const displacement = this.value - this.target;
    const springForce = -this.stiffness * displacement;
    const dampingForce = -this.damping * this.velocity;
    const acceleration = (springForce + dampingForce) / this.mass;

    this.velocity += acceleration * dt;
    this.value += this.velocity * dt;

    if (this.isSettled) {
      this.value = this.target;
      this.velocity = 0;
    }

    return this.value;
  }
}

export class Spring2D {
  public readonly x: SpringValue;
  public readonly y: SpringValue;

  constructor(ix = 0, iy = 0, config: SpringConfig = {}) {
    this.x = new SpringValue(ix, config);
    this.y = new SpringValue(iy, config);
  }

  setTarget(tx: number, ty: number): void {
    this.x.setTarget(tx);
    this.y.setTarget(ty);
  }

  step(dtSeconds: number): { x: number; y: number } {
    return {
      x: this.x.step(dtSeconds),
      y: this.y.step(dtSeconds),
    };
  }
}
