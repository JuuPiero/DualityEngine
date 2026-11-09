/**
 * DualityEngine TypeScript API (module "duality").
 *
 * Unity-like scripting for DualityEngine on QuickJS: write a class extending MonoBehaviour in
 * any .ts file under the project's Assets/ folder, export it, and it shows up in the Editor's
 * Add Component > Scripts list next to the C++ scripts. This file is bundled (esbuild) INTO the
 * game's script bundle; the engine only provides the small native bridge `__duality`
 * (DualityEngine/Source/Scripting/JS/JsBindings.cpp).
 *
 * Coordinate conventions are the engine's own: positions are world units (the 2D renderer draws
 * PPU pixels per unit, 100 by default) and +Y points DOWN (gravity is +Y) in 2D and 3D alike, so `Vector2.up` is (0, -1). Rotations are
 * Euler degrees (Transform.Rotation), composed as Rz * Ry * Rx.
 */

/* eslint-disable @typescript-eslint/no-explicit-any */

const N: any = (globalThis as any).__duality;
/** Shared scratch buffer the native side writes vector results into (no per-call arrays). */
const F = new Float64Array(16);

const SLOT_SCALE = 4294967296; // entity id = scene slot * 2^32 + entt handle
function slotOf(id: number): number { return Math.floor(id / SLOT_SCALE); }
function currentScene(): number { return N.curScene(); }

// =============================================================================================
// Math
// =============================================================================================

export class Vector2 {
  constructor(public x = 0, public y = 0) {}
  static get zero() { return new Vector2(0, 0); }
  static get one() { return new Vector2(1, 1); }
  /** (0, -1): +Y points down in DualityEngine. */
  static get up() { return new Vector2(0, -1); }
  static get down() { return new Vector2(0, 1); }
  static get left() { return new Vector2(-1, 0); }
  static get right() { return new Vector2(1, 0); }

  set(x: number, y: number): this { this.x = x; this.y = y; return this; }
  clone(): Vector2 { return new Vector2(this.x, this.y); }
  copyFrom(v: Vector2): this { this.x = v.x; this.y = v.y; return this; }
  add(v: Vector2): Vector2 { return new Vector2(this.x + v.x, this.y + v.y); }
  sub(v: Vector2): Vector2 { return new Vector2(this.x - v.x, this.y - v.y); }
  mul(s: number): Vector2 { return new Vector2(this.x * s, this.y * s); }
  div(s: number): Vector2 { return new Vector2(this.x / s, this.y / s); }
  scale(v: Vector2): Vector2 { return new Vector2(this.x * v.x, this.y * v.y); }
  negate(): Vector2 { return new Vector2(-this.x, -this.y); }
  dot(v: Vector2): number { return this.x * v.x + this.y * v.y; }
  get sqrMagnitude(): number { return this.x * this.x + this.y * this.y; }
  get magnitude(): number { return Math.sqrt(this.x * this.x + this.y * this.y); }
  get normalized(): Vector2 { const m = this.magnitude; return m > 1e-6 ? new Vector2(this.x / m, this.y / m) : new Vector2(); }
  normalize(): this { const m = this.magnitude; if (m > 1e-6) { this.x /= m; this.y /= m; } else { this.x = 0; this.y = 0; } return this; }
  equals(v: Vector2, epsilon = 1e-5): boolean { return Math.abs(this.x - v.x) <= epsilon && Math.abs(this.y - v.y) <= epsilon; }
  toVector3(z = 0): Vector3 { return new Vector3(this.x, this.y, z); }
  toString(): string { return `(${fmtNum(this.x)}, ${fmtNum(this.y)})`; }

  static dot(a: Vector2, b: Vector2): number { return a.x * b.x + a.y * b.y; }
  static distance(a: Vector2, b: Vector2): number { return Math.hypot(a.x - b.x, a.y - b.y); }
  static lerp(a: Vector2, b: Vector2, t: number): Vector2 { t = Mathf.clamp01(t); return new Vector2(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t); }
  static moveTowards(current: Vector2, target: Vector2, maxDelta: number): Vector2 {
    const dx = target.x - current.x, dy = target.y - current.y, d = Math.hypot(dx, dy);
    if (d <= maxDelta || d === 0) return target.clone();
    return new Vector2(current.x + dx / d * maxDelta, current.y + dy / d * maxDelta);
  }
  static clampMagnitude(v: Vector2, max: number): Vector2 { const m = v.magnitude; return m > max ? v.mul(max / m) : v.clone(); }
  /** Unsigned angle in degrees between two vectors. */
  static angle(a: Vector2, b: Vector2): number {
    const d = Math.sqrt(a.sqrMagnitude * b.sqrMagnitude);
    return d < 1e-12 ? 0 : Math.acos(Mathf.clamp(a.dot(b) / d, -1, 1)) * Mathf.Rad2Deg;
  }
}

export class Vector3 {
  constructor(public x = 0, public y = 0, public z = 0) {}
  static get zero() { return new Vector3(0, 0, 0); }
  static get one() { return new Vector3(1, 1, 1); }
  /** (0, -1, 0): +Y points down in DualityEngine. */
  static get up() { return new Vector3(0, -1, 0); }
  static get down() { return new Vector3(0, 1, 0); }
  static get left() { return new Vector3(-1, 0, 0); }
  static get right() { return new Vector3(1, 0, 0); }
  static get forward() { return new Vector3(0, 0, 1); }
  static get back() { return new Vector3(0, 0, -1); }

  set(x: number, y: number, z: number): this { this.x = x; this.y = y; this.z = z; return this; }
  clone(): Vector3 { return new Vector3(this.x, this.y, this.z); }
  copyFrom(v: Vector3): this { this.x = v.x; this.y = v.y; this.z = v.z; return this; }
  add(v: Vector3): Vector3 { return new Vector3(this.x + v.x, this.y + v.y, this.z + v.z); }
  sub(v: Vector3): Vector3 { return new Vector3(this.x - v.x, this.y - v.y, this.z - v.z); }
  mul(s: number): Vector3 { return new Vector3(this.x * s, this.y * s, this.z * s); }
  div(s: number): Vector3 { return new Vector3(this.x / s, this.y / s, this.z / s); }
  scale(v: Vector3): Vector3 { return new Vector3(this.x * v.x, this.y * v.y, this.z * v.z); }
  negate(): Vector3 { return new Vector3(-this.x, -this.y, -this.z); }
  dot(v: Vector3): number { return this.x * v.x + this.y * v.y + this.z * v.z; }
  cross(v: Vector3): Vector3 { return new Vector3(this.y * v.z - this.z * v.y, this.z * v.x - this.x * v.z, this.x * v.y - this.y * v.x); }
  get sqrMagnitude(): number { return this.x * this.x + this.y * this.y + this.z * this.z; }
  get magnitude(): number { return Math.sqrt(this.sqrMagnitude); }
  get normalized(): Vector3 { const m = this.magnitude; return m > 1e-6 ? this.div(m) : new Vector3(); }
  normalize(): this { const m = this.magnitude; if (m > 1e-6) { this.x /= m; this.y /= m; this.z /= m; } else this.set(0, 0, 0); return this; }
  equals(v: Vector3, epsilon = 1e-5): boolean { return Math.abs(this.x - v.x) <= epsilon && Math.abs(this.y - v.y) <= epsilon && Math.abs(this.z - v.z) <= epsilon; }
  toVector2(): Vector2 { return new Vector2(this.x, this.y); }
  toString(): string { return `(${fmtNum(this.x)}, ${fmtNum(this.y)}, ${fmtNum(this.z)})`; }

  static dot(a: Vector3, b: Vector3): number { return a.dot(b); }
  static cross(a: Vector3, b: Vector3): Vector3 { return a.cross(b); }
  static distance(a: Vector3, b: Vector3): number { return Math.hypot(a.x - b.x, a.y - b.y, a.z - b.z); }
  static lerp(a: Vector3, b: Vector3, t: number): Vector3 { t = Mathf.clamp01(t); return new Vector3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }
  static moveTowards(current: Vector3, target: Vector3, maxDelta: number): Vector3 {
    const d = target.sub(current), m = d.magnitude;
    return m <= maxDelta || m === 0 ? target.clone() : current.add(d.mul(maxDelta / m));
  }
  static clampMagnitude(v: Vector3, max: number): Vector3 { const m = v.magnitude; return m > max ? v.mul(max / m) : v.clone(); }
  static angle(a: Vector3, b: Vector3): number {
    const d = Math.sqrt(a.sqrMagnitude * b.sqrMagnitude);
    return d < 1e-12 ? 0 : Math.acos(Mathf.clamp(a.dot(b) / d, -1, 1)) * Mathf.Rad2Deg;
  }
}

export class Vector4 {
  constructor(public x = 0, public y = 0, public z = 0, public w = 0) {}
  clone(): Vector4 { return new Vector4(this.x, this.y, this.z, this.w); }
  toString(): string { return `(${fmtNum(this.x)}, ${fmtNum(this.y)}, ${fmtNum(this.z)}, ${fmtNum(this.w)})`; }
}

/** Rotation quaternion. Euler angles follow the engine's Rz * Ry * Rx composition (degrees). */
export class Quaternion {
  constructor(public x = 0, public y = 0, public z = 0, public w = 1) {}
  static get identity() { return new Quaternion(0, 0, 0, 1); }

  static euler(x: number, y: number, z: number): Quaternion {
    const hx = x * Mathf.Deg2Rad * 0.5, hy = y * Mathf.Deg2Rad * 0.5, hz = z * Mathf.Deg2Rad * 0.5;
    const cx = Math.cos(hx), sx = Math.sin(hx), cy = Math.cos(hy), sy = Math.sin(hy), cz = Math.cos(hz), sz = Math.sin(hz);
    // q = qz * qy * qx
    return new Quaternion(
      sx * cy * cz - cx * sy * sz,
      cx * sy * cz + sx * cy * sz,
      cx * cy * sz - sx * sy * cz,
      cx * cy * cz + sx * sy * sz);
  }
  static angleAxis(angle: number, axis: Vector3): Quaternion {
    const a = axis.normalized, h = angle * Mathf.Deg2Rad * 0.5, s = Math.sin(h);
    return new Quaternion(a.x * s, a.y * s, a.z * s, Math.cos(h));
  }
  get eulerAngles(): Vector3 {
    const { x, y, z, w } = this;
    const roll = Math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y));
    const pitch = Math.asin(Mathf.clamp(2 * (w * y - z * x), -1, 1));
    const yaw = Math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z));
    return new Vector3(roll * Mathf.Rad2Deg, pitch * Mathf.Rad2Deg, yaw * Mathf.Rad2Deg);
  }
  multiply(q: Quaternion): Quaternion {
    const a = this;
    return new Quaternion(
      a.w * q.x + a.x * q.w + a.y * q.z - a.z * q.y,
      a.w * q.y - a.x * q.z + a.y * q.w + a.z * q.x,
      a.w * q.z + a.x * q.y - a.y * q.x + a.z * q.w,
      a.w * q.w - a.x * q.x - a.y * q.y - a.z * q.z);
  }
  /** Rotates a vector by this quaternion. */
  multiplyVector(v: Vector3): Vector3 {
    const { x, y, z, w } = this;
    const ix = w * v.x + y * v.z - z * v.y, iy = w * v.y + z * v.x - x * v.z, iz = w * v.z + x * v.y - y * v.x, iw = -x * v.x - y * v.y - z * v.z;
    return new Vector3(ix * w + iw * -x + iy * -z - iz * -y, iy * w + iw * -y + iz * -x - ix * -z, iz * w + iw * -z + ix * -y - iy * -x);
  }
  get inverse(): Quaternion { const n = this.x * this.x + this.y * this.y + this.z * this.z + this.w * this.w || 1; return new Quaternion(-this.x / n, -this.y / n, -this.z / n, this.w / n); }
  static slerp(a: Quaternion, b: Quaternion, t: number): Quaternion {
    t = Mathf.clamp01(t);
    let cos = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    let bx = b.x, by = b.y, bz = b.z, bw = b.w;
    if (cos < 0) { cos = -cos; bx = -bx; by = -by; bz = -bz; bw = -bw; }
    if (cos > 0.9995) {
      const q = new Quaternion(a.x + (bx - a.x) * t, a.y + (by - a.y) * t, a.z + (bz - a.z) * t, a.w + (bw - a.w) * t);
      const n = Math.hypot(q.x, q.y, q.z, q.w); q.x /= n; q.y /= n; q.z /= n; q.w /= n; return q;
    }
    const theta = Math.acos(cos), s = Math.sin(theta), wa = Math.sin((1 - t) * theta) / s, wb = Math.sin(t * theta) / s;
    return new Quaternion(a.x * wa + bx * wb, a.y * wa + by * wb, a.z * wa + bz * wb, a.w * wa + bw * wb);
  }
  toString(): string { return `(${fmtNum(this.x)}, ${fmtNum(this.y)}, ${fmtNum(this.z)}, ${fmtNum(this.w)})`; }
}

/** RGBA color, components 0..1. */
export class Color {
  constructor(public r = 1, public g = 1, public b = 1, public a = 1) {}
  static get white() { return new Color(1, 1, 1, 1); }
  static get black() { return new Color(0, 0, 0, 1); }
  static get red() { return new Color(1, 0, 0, 1); }
  static get green() { return new Color(0, 1, 0, 1); }
  static get blue() { return new Color(0, 0, 1, 1); }
  static get yellow() { return new Color(1, 0.92, 0.016, 1); }
  static get cyan() { return new Color(0, 1, 1, 1); }
  static get magenta() { return new Color(1, 0, 1, 1); }
  static get gray() { return new Color(0.5, 0.5, 0.5, 1); }
  static get clear() { return new Color(0, 0, 0, 0); }
  clone(): Color { return new Color(this.r, this.g, this.b, this.a); }
  static lerp(a: Color, b: Color, t: number): Color { t = Mathf.clamp01(t); return new Color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t); }
  toString(): string { return `RGBA(${fmtNum(this.r)}, ${fmtNum(this.g)}, ${fmtNum(this.b)}, ${fmtNum(this.a)})`; }
}

export class Mathf {
  static readonly PI = Math.PI;
  static readonly Deg2Rad = Math.PI / 180;
  static readonly Rad2Deg = 180 / Math.PI;
  static readonly Epsilon = 1.401298e-45;
  static readonly Infinity = Number.POSITIVE_INFINITY;
  static clamp(v: number, min: number, max: number): number { return v < min ? min : v > max ? max : v; }
  static clamp01(v: number): number { return v < 0 ? 0 : v > 1 ? 1 : v; }
  static lerp(a: number, b: number, t: number): number { return a + (b - a) * Mathf.clamp01(t); }
  static lerpUnclamped(a: number, b: number, t: number): number { return a + (b - a) * t; }
  static inverseLerp(a: number, b: number, v: number): number { return a !== b ? Mathf.clamp01((v - a) / (b - a)) : 0; }
  static moveTowards(current: number, target: number, maxDelta: number): number {
    return Math.abs(target - current) <= maxDelta ? target : current + Math.sign(target - current) * maxDelta;
  }
  static repeat(t: number, length: number): number { return Mathf.clamp(t - Math.floor(t / length) * length, 0, length); }
  static pingPong(t: number, length: number): number { t = Mathf.repeat(t, length * 2); return length - Math.abs(t - length); }
  static deltaAngle(current: number, target: number): number { let d = Mathf.repeat(target - current, 360); if (d > 180) d -= 360; return d; }
  static lerpAngle(a: number, b: number, t: number): number { return a + Mathf.deltaAngle(a, b) * Mathf.clamp01(t); }
  static moveTowardsAngle(current: number, target: number, maxDelta: number): number {
    const d = Mathf.deltaAngle(current, target);
    return -maxDelta < d && d < maxDelta ? target : Mathf.moveTowards(current, current + d, maxDelta);
  }
  static approximately(a: number, b: number): boolean { return Math.abs(b - a) < Math.max(1e-6 * Math.max(Math.abs(a), Math.abs(b)), 1e-8); }
  static sign(v: number): number { return v >= 0 ? 1 : -1; }
  static abs = Math.abs; static min = Math.min; static max = Math.max; static sqrt = Math.sqrt; static pow = Math.pow;
  static sin = Math.sin; static cos = Math.cos; static tan = Math.tan; static atan2 = Math.atan2; static floor = Math.floor; static ceil = Math.ceil;
  static floorToInt(v: number): number { return Math.floor(v); }
  static ceilToInt(v: number): number { return Math.ceil(v); }
  static roundToInt(v: number): number { return Math.round(v); }
  static smoothStep(from: number, to: number, t: number): number { t = Mathf.clamp01(t); t = t * t * (3 - 2 * t); return from + (to - from) * t; }
}

export class Random {
  /** Float in [0, 1). */
  static get value(): number { return Math.random(); }
  /** Float in [min, max). */
  static range(min: number, max: number): number { return min + Math.random() * (max - min); }
  /** Integer in [min, max) -- Unity's Random.Range(int, int). */
  static rangeInt(min: number, max: number): number { return min + Math.floor(Math.random() * (max - min)); }
  static get insideUnitCircle(): Vector2 { const a = Math.random() * Math.PI * 2, r = Math.sqrt(Math.random()); return new Vector2(Math.cos(a) * r, Math.sin(a) * r); }
  static get onUnitCircle(): Vector2 { const a = Math.random() * Math.PI * 2; return new Vector2(Math.cos(a), Math.sin(a)); }
}

function fmtNum(v: number): string { return Number.isInteger(v) ? v.toFixed(1) : (Math.round(v * 100) / 100).toString(); }

// =============================================================================================
// Debug / console
// =============================================================================================

function stringify(value: any): string {
  if (typeof value === 'string') return value;
  if (value === null) return 'null';
  if (value === undefined) return 'undefined';
  if (value instanceof Error) return `${value.name}: ${value.message}`;
  if (typeof value === 'object') {
    if (typeof value.toString === 'function' && value.toString !== Object.prototype.toString) return String(value);
    try { return JSON.stringify(value); } catch { return String(value); }
  }
  return String(value);
}
function join(args: any[]): string { return args.map(stringify).join(' '); }

/** Logs go to the Editor's Console panel (and stdout / the 3DS debug output). */
export class Debug {
  static log(...args: any[]): void { N.log(0, join(args)); }
  static warn(...args: any[]): void { N.log(1, join(args)); }
  static error(...args: any[]): void { N.log(2, join(args)); }
  static logWarning(...args: any[]): void { N.log(1, join(args)); }
  static logError(...args: any[]): void { N.log(2, join(args)); }
  static assert(condition: any, ...args: any[]): void { if (!condition) N.log(2, 'Assertion failed' + (args.length ? ': ' + join(args) : '')); }
}

if (!(globalThis as any).console) {
  (globalThis as any).console = { log: Debug.log, info: Debug.log, debug: Debug.log, warn: Debug.warn, error: Debug.error };
}

// =============================================================================================
// Time / Input
// =============================================================================================

export class Time {
  /** Seconds since last frame (the fixed step inside fixedUpdate). */
  static get deltaTime(): number { return N.dt(); }
  static get unscaledDeltaTime(): number { return N.dt(); }
  /** Seconds since Play started. */
  static get time(): number { return N.time(); }
  static get frameCount(): number { return N.frame(); }
  static readonly fixedDeltaTime = 1 / 60;
  static get inFixedTimeStep(): boolean { return N.inFixed(); }
  static get realtimeSinceStartup(): number { return N.realtime(); }
}

/** Keys and 3DS buttons (DualityEngine/Input/KeyCode.h). Desktop reports the keyboard codes,
 *  the 3DS reports the Gamepad* codes. */
export const KeyCode = {
  W: 0, A: 1, S: 2, D: 3, UpArrow: 4, DownArrow: 5, LeftArrow: 6, RightArrow: 7, Space: 8, Return: 9, Escape: 10,
  GamepadA: 11, GamepadB: 12, GamepadX: 13, GamepadY: 14, GamepadL: 15, GamepadR: 16, GamepadStart: 17, GamepadSelect: 18,
  GamepadDPadUp: 19, GamepadDPadDown: 20, GamepadDPadLeft: 21, GamepadDPadRight: 22,
} as const;
export type KeyCode = typeof KeyCode[keyof typeof KeyCode];

const keyAliases: Record<string, number> = {
  w: 0, a: 1, s: 2, d: 3, up: 4, uparrow: 4, down: 5, downarrow: 5, left: 6, leftarrow: 6, right: 7, rightarrow: 7,
  space: 8, enter: 9, return: 9, escape: 10, esc: 10,
  gamepada: 11, buttona: 11, gamepadb: 12, buttonb: 12, gamepadx: 13, buttonx: 13, gamepady: 14, buttony: 14,
  gamepadl: 15, buttonl: 15, gamepadr: 16, buttonr: 16, gamepadstart: 17, start: 17, gamepadselect: 18, select: 18,
  gamepaddpadup: 19, dpadup: 19, gamepaddpaddown: 20, dpaddown: 20, gamepaddpadleft: 21, dpadleft: 21, gamepaddpadright: 22, dpadright: 22,
};
function keyCodeOf(key: number | string): number {
  if (typeof key === 'number') return key;
  const code = keyAliases[key.replace(/[\s_-]/g, '').toLowerCase()];
  return code === undefined ? -1 : code;
}
const buttonKeys: Record<string, number[]> = {
  jump: [KeyCode.Space, KeyCode.GamepadA], fire1: [KeyCode.Space, KeyCode.GamepadA], fire2: [KeyCode.Escape, KeyCode.GamepadB],
  submit: [KeyCode.Return, KeyCode.GamepadA], cancel: [KeyCode.Escape, KeyCode.GamepadB],
};

export class Input {
  static getKey(key: KeyCode | string): boolean { return N.key(keyCodeOf(key), 0); }
  static getKeyDown(key: KeyCode | string): boolean { return N.key(keyCodeOf(key), 1); }
  static getKeyUp(key: KeyCode | string): boolean { return N.key(keyCodeOf(key), 2); }
  /** "Horizontal" / "Vertical" in [-1, 1] (keyboard on desktop, Circle Pad on 3DS; Vertical is +down). */
  static getAxis(name: string): number { return N.axis(name); }
  static getAxisRaw(name: string): number { const v = N.axis(name); return v > 0.01 ? 1 : v < -0.01 ? -1 : 0; }
  /** "Jump" / "Fire1" / "Fire2" / "Submit" / "Cancel" mapped to keyboard + 3DS buttons. */
  static getButton(name: string): boolean { return (buttonKeys[name.toLowerCase()] ?? []).some((k) => N.key(k, 0)); }
  static getButtonDown(name: string): boolean { return (buttonKeys[name.toLowerCase()] ?? []).some((k) => N.key(k, 1)); }
  static getButtonUp(name: string): boolean { return (buttonKeys[name.toLowerCase()] ?? []).some((k) => N.key(k, 2)); }
  /** Mouse (desktop) / touch screen (3DS). Only button 0 exists. */
  static getMouseButton(button = 0): boolean { return button === 0 && N.ptr(); }
  static getMouseButtonDown(button = 0): boolean { return button === 0 && N.ptrPressed(); }
  static getMouseButtonUp(button = 0): boolean { return button === 0 && N.ptrUp(); }
  /** Pointer position in the local pixels of `pointerScreen` (top-left origin, Y down). */
  static get mousePosition(): Vector3 { N.ptr(); return new Vector3(F[0], F[1], 0); }
  /** 0 = top screen, 1 = bottom (touch) screen. */
  static get pointerScreen(): number { N.ptr(); return F[2]; }
  static get touchCount(): number { return N.ptr() ? 1 : 0; }
}

// =============================================================================================
// Assets
// =============================================================================================

/** A reference to a project asset by its .meta GUID (what an asset field stores). */
export class Asset {
  constructor(public readonly guid: string) {}
  /** Project path the GUID resolves to ("" if unknown). */
  get path(): string { return N.assetPath(this.guid); }
  toString(): string { return `${(this.constructor as any).name}(${this.guid})`; }
}
export class Prefab extends Asset {}
export class AudioClip extends Asset {}
export class Texture extends Asset {}
export class Material extends Asset {}
export class SceneAsset extends Asset {}

/** A ScriptableObject data asset (.asset, class defined in C++ GameScripts): fields by name. */
export class ScriptableObject extends Asset {
  get className(): string { const info = N.soInfo(this.guid); return info ? info[0] : ''; }
  get fieldNames(): string[] { const info = N.soInfo(this.guid); return info ? info.slice(1) : []; }
  get(field: string): any { return N.soGet(this.guid, field); }
  set(field: string, value: any): boolean { return N.soSet(this.guid, field, normalize(value)); }
  static load(guid: string): ScriptableObject | null { return N.soInfo(guid) ? new ScriptableObject(guid) : null; }
}

// =============================================================================================
// Object model: GameObject, Component, Transform, MonoBehaviour
// =============================================================================================

const goCache = new Map<number, GameObject>();

/** Normalizes script values for the native side (vectors -> arrays, objects -> ids/guids). */
function normalize(value: any): any {
  if (value instanceof Vector2) return [value.x, value.y];
  if (value instanceof Vector3) return [value.x, value.y, value.z];
  if (value instanceof Vector4) return [value.x, value.y, value.z, value.w];
  if (value instanceof Quaternion) return value.eulerAngles;
  if (value instanceof Color) return [value.r, value.g, value.b, value.a];
  if (value instanceof Asset) return value.guid;
  if (value instanceof GameObject) return value._id;
  if (value instanceof Component || value instanceof MonoBehaviour) return value.gameObject._id;
  if (value === null || value === undefined) return value;
  if (Array.isArray(value)) return value.map(normalize);
  return value;
}

/** Unity's GameObject: an entity in a scene. Wrappers are cached, so `===` compares identity. */
export class GameObject {
  /** @internal entity id (scene slot * 2^32 + handle). */
  _id!: number;
  /** @internal built-in component wrapper cache. */
  _comps!: Map<any, Component> | null;
  /** @internal */
  _transform!: Transform | null;

  /** Creates a new, empty entity in the current scene. */
  constructor(name = 'GameObject') {
    const id = N.create(currentScene(), name);
    if (id < 0) throw new Error('new GameObject(): no running scene (call it from a script callback)');
    initGameObject(this, id);
    goCache.set(id, this);
  }

  /** @internal Wraps an existing entity id (null for -1/stale). */
  static _wrap(id: number): GameObject | null {
    if (id === undefined || id < 0) return null;
    let go = goCache.get(id);
    if (!go) {
      go = Object.create(GameObject.prototype) as GameObject;
      initGameObject(go, id);
      goCache.set(id, go);
    }
    return go;
  }

  get name(): string { return N.getName(this._id); }
  set name(value: string) { N.setName(this._id, value); }
  get tag(): string { return N.getTag(this._id); }
  set tag(value: string) { N.setTag(this._id, value); }
  compareTag(tag: string): boolean { return N.getTag(this._id) === tag; }
  get activeSelf(): boolean { return N.activeSelf(this._id); }
  get activeInHierarchy(): boolean { return N.activeInHierarchy(this._id); }
  setActive(value: boolean): void { N.setActive(this._id, !!value); }
  /** False once the entity was destroyed (Unity's `obj == null`). */
  get isValid(): boolean { return N.valid(this._id); }
  get destroyed(): boolean { return !N.valid(this._id); }
  get transform(): Transform { return this._transform ?? (this._transform = new Transform(this, 'Transform')); }

  getComponent<T>(type: ComponentType<T> | string): T | null { return findComponents(this, type, true)[0] ?? null; }
  getComponents<T>(type: ComponentType<T> | string): T[] { return findComponents(this, type, false); }
  getComponentInChildren<T>(type: ComponentType<T> | string, includeInactive = false): T | null {
    const stack: GameObject[] = [this];
    while (stack.length) {
      const go = stack.shift()!;
      if (includeInactive || go.activeInHierarchy) { const c = go.getComponent(type); if (c) return c; }
      const t = go.transform;
      for (let i = 0; i < t.childCount; i++) stack.push(t.getChild(i)!.gameObject);
    }
    return null;
  }
  getComponentsInChildren<T>(type: ComponentType<T> | string, includeInactive = false): T[] {
    const result: T[] = [];
    const visit = (go: GameObject) => {
      if (includeInactive || go.activeInHierarchy) result.push(...go.getComponents(type));
      const t = go.transform;
      for (let i = 0; i < t.childCount; i++) visit(t.getChild(i)!.gameObject);
    };
    visit(this);
    return result;
  }
  getComponentInParent<T>(type: ComponentType<T> | string): T | null {
    for (let go: GameObject | null = this; go; go = go.transform.parent?.gameObject ?? null) {
      const c = go.getComponent(type);
      if (c) return c;
    }
    return null;
  }
  /** Adds a built-in component (by class or Inspector name) or a script (TS class / C++ class
   *  name). During Play a script is created and Awakes immediately. */
  addComponent<T>(type: ComponentType<T> | string): T | null {
    if (typeof type === 'function' && isScriptClass(type)) {
      const name = (type as any).__dualityName ?? type.name;
      const slot = N.addScript(this._id, name);
      return slot < 0 ? null : (N.scriptObject(this._id, slot) as T | null);
    }
    if (typeof type === 'string' && !N.add(this._id, type)) {
      const slot = N.addScript(this._id, type);
      return slot < 0 ? null : ((N.scriptObject(this._id, slot) ?? new NativeScript(this, slot)) as any);
    }
    if (typeof type === 'function') {
      const names: readonly string[] = (type as any).typeNames ?? [];
      if (!names.length || !N.add(this._id, names[0])) return null;
    }
    return this.getComponent(type);
  }
  /** Calls `method()` on every TypeScript script of this GameObject (Unity's SendMessage). */
  sendMessage(method: string): void {
    const count = N.scriptCount(this._id);
    for (let i = 0; i < count; i++) N.scriptCall(this._id, i, method);
  }

  static find(name: string): GameObject | null { return GameObject._wrap(N.find(currentScene(), name)); }
  static findWithTag(tag: string): GameObject | null { const ids: number[] = N.findTag(currentScene(), tag); return ids.length ? GameObject._wrap(ids[0]) : null; }
  static findGameObjectsWithTag(tag: string): GameObject[] { return (N.findTag(currentScene(), tag) as number[]).map((id) => GameObject._wrap(id)!); }
  toString(): string { return `GameObject(${this.isValid ? this.name : 'destroyed'})`; }
}

function initGameObject(go: GameObject, id: number): void {
  go._id = id;
  go._comps = null;
  go._transform = null;
}

/** A built-in component class (Rigidbody2D, SpriteRenderer, ...), a script class, or a name. */
export type ComponentType<T> = (abstract new (...args: any[]) => T) | (new (...args: any[]) => T);

function isScriptClass(type: any): boolean { return typeof type === 'function' && (type === MonoBehaviour || type.prototype instanceof MonoBehaviour); }
function isBuiltinClass(type: any): boolean { return typeof type === 'function' && (type === Component || type.prototype instanceof Component); }

function findComponents(go: GameObject, type: any, firstOnly: boolean): any[] {
  const id = go._id;
  const result: any[] = [];
  if (type === Transform) return N.valid(id) ? [go.transform] : [];
  if (isScriptClass(type)) {
    const count = N.scriptCount(id);
    for (let i = 0; i < count; i++) {
      const obj = N.scriptObject(id, i);
      if (obj && obj instanceof type) {
        result.push(obj);
        if (firstOnly) break;
      }
    }
    return result;
  }
  if (isBuiltinClass(type)) {
    const names: readonly string[] = type.typeNames ?? [];
    for (const name of names) {
      if (!N.has(id, name)) continue;
      result.push(builtinWrapper(go, type, name));
      if (firstOnly) break;
    }
    return result;
  }
  if (typeof type === 'string') {
    const count = N.scriptCount(id);
    for (let i = 0; i < count; i++) {
      if (N.scriptName(id, i) !== type) continue;
      result.push(N.scriptObject(id, i) ?? new NativeScript(go, i));
      if (firstOnly) return result;
    }
    if (N.has(id, type)) result.push(builtinWrapper(go, Component, type));
    return result;
  }
  return result;
}

function builtinWrapper(go: GameObject, type: any, name: string): Component {
  const cache = go._comps ?? (go._comps = new Map());
  const key = type === Component ? name : type;
  let wrapper = cache.get(key);
  if (!wrapper) {
    wrapper = new type(go, name) as Component;
    cache.set(key, wrapper);
  }
  return wrapper;
}

const fieldIds = new Map<string, number>();
function fieldId(typeName: string, field: string): number {
  const key = typeName + '\u0001' + field;
  let id = fieldIds.get(key);
  if (id === undefined) { id = N.fid(typeName, field) as number; fieldIds.set(key, id); }
  return id;
}

/** Base of the built-in component wrappers. Any Inspector field is reachable by its Inspector
 *  name through get()/set(), e.g. `getComponent('Particle System').set('Emission Rate', 20)`. */
export class Component {
  /** Inspector names of the C++ component types this wrapper class stands for. */
  static readonly typeNames: readonly string[] = [];
  constructor(readonly gameObject: GameObject, readonly typeName: string) {}
  get transform(): Transform { return this.gameObject.transform; }
  get name(): string { return this.gameObject.name; }
  /** Reads a field by its Inspector name (vectors/colors come back as arrays). */
  get(field: string): any { return N.fget(this.gameObject._id, fieldId(this.typeName, field)); }
  set(field: string, value: any): boolean { return N.fset(this.gameObject._id, fieldId(this.typeName, field), normalize(value)); }
  get enabled(): boolean { const v = this.get('Enabled'); return v === undefined ? true : !!v; }
  set enabled(value: boolean) { this.set('Enabled', !!value); }
  protected getVec2(field: string): Vector2 { const a = this.get(field); return a ? new Vector2(a[0], a[1]) : new Vector2(); }
  protected getVec3(field: string): Vector3 { const a = this.get(field); return a ? new Vector3(a[0], a[1], a[2]) : new Vector3(); }
  protected getColor(field: string): Color { const a = this.get(field); return a ? new Color(a[0], a[1], a[2], a[3]) : new Color(); }
  protected getAsset<T extends Asset>(field: string, type: new (guid: string) => T): T | null { const g = this.get(field); return g ? new type(g) : null; }
  toString(): string { return `${this.typeName}(${this.gameObject.name})`; }
}

export enum Space { World = 0, Self = 1 }

export class Transform extends Component {
  static readonly typeNames = ['Transform'] as const;

  get position(): Vector3 { N.tget(this.gameObject._id, 1); return new Vector3(F[0], F[1], F[2]); }
  set position(v: Vector3) { N.tset(this.gameObject._id, 1, v.x, v.y, v.z); }
  get localPosition(): Vector3 { N.tget(this.gameObject._id, 0); return new Vector3(F[0], F[1], F[2]); }
  set localPosition(v: Vector3) { N.tset(this.gameObject._id, 0, v.x, v.y, v.z); }
  /** World Euler angles in degrees. */
  get eulerAngles(): Vector3 { N.tget(this.gameObject._id, 3); return new Vector3(F[0], F[1], F[2]); }
  set eulerAngles(v: Vector3) { N.tset(this.gameObject._id, 3, v.x, v.y, v.z); }
  get localEulerAngles(): Vector3 { N.tget(this.gameObject._id, 2); return new Vector3(F[0], F[1], F[2]); }
  set localEulerAngles(v: Vector3) { N.tset(this.gameObject._id, 2, v.x, v.y, v.z); }
  get rotation(): Quaternion { const e = this.eulerAngles; return Quaternion.euler(e.x, e.y, e.z); }
  set rotation(q: Quaternion) { this.eulerAngles = q.eulerAngles; }
  get localRotation(): Quaternion { const e = this.localEulerAngles; return Quaternion.euler(e.x, e.y, e.z); }
  set localRotation(q: Quaternion) { this.localEulerAngles = q.eulerAngles; }
  get localScale(): Vector3 { N.tget(this.gameObject._id, 4); return new Vector3(F[0], F[1], F[2]); }
  set localScale(v: Vector3) { N.tset(this.gameObject._id, 4, v.x, v.y, v.z); }
  get lossyScale(): Vector3 { N.tget(this.gameObject._id, 5); return new Vector3(F[0], F[1], F[2]); }

  /** Allocation-free accessors for hot paths: write into `out`. */
  getPosition(out: Vector3): Vector3 { N.tget(this.gameObject._id, 1); return out.set(F[0], F[1], F[2]); }
  getLocalPosition(out: Vector3): Vector3 { N.tget(this.gameObject._id, 0); return out.set(F[0], F[1], F[2]); }
  setPosition(x: number, y: number, z: number): void { N.tset(this.gameObject._id, 1, x, y, z); }
  setLocalPosition(x: number, y: number, z: number): void { N.tset(this.gameObject._id, 0, x, y, z); }

  get forward(): Vector3 { return this.rotation.multiplyVector(Vector3.forward); }
  get right(): Vector3 { return this.rotation.multiplyVector(Vector3.right); }
  get up(): Vector3 { return this.rotation.multiplyVector(Vector3.up); }

  /** Moves by (x, y, z) in local-parent space (Space.World also adds directly: the engine's
   *  hierarchy is 2D-affine, so for root objects both are identical). */
  translate(x: number | Vector3 | Vector2, y?: number, z?: number): void {
    if (typeof x === 'number') N.translate(this.gameObject._id, x, y ?? 0, z ?? 0);
    else N.translate(this.gameObject._id, x.x, x.y, (x as Vector3).z ?? 0);
  }
  /** Adds Euler degrees to the local rotation (2D: rotate(0, 0, degrees)). */
  rotate(x: number | Vector3, y?: number, z?: number): void {
    if (typeof x === 'number') N.rotate(this.gameObject._id, x, y ?? 0, z ?? 0);
    else N.rotate(this.gameObject._id, x.x, x.y, x.z);
  }

  get parent(): Transform | null { return GameObject._wrap(N.parent(this.gameObject._id))?.transform ?? null; }
  set parent(value: Transform | null) { this.setParent(value, true); }
  setParent(parent: Transform | GameObject | null, worldPositionStays = true): void {
    const pid = parent ? (parent instanceof GameObject ? parent._id : parent.gameObject._id) : -1;
    N.setParent(this.gameObject._id, pid, worldPositionStays);
  }
  get root(): Transform { let t: Transform = this; for (let p = t.parent; p; p = p.parent) t = p; return t; }
  get childCount(): number { return N.childCount(this.gameObject._id); }
  getChild(index: number): Transform | null { return GameObject._wrap(N.child(this.gameObject._id, index))?.transform ?? null; }
  /** Direct child by name ("a/b" paths walk down). */
  find(path: string): Transform | null {
    let current: Transform | null = this;
    for (const part of path.split('/')) {
      if (!current) return null;
      let next: Transform | null = null;
      for (let i = 0; i < current.childCount; i++) { const c = current.getChild(i); if (c && c.gameObject.name === part) { next = c; break; } }
      current = next;
    }
    return current;
  }
}

// --- built-in component wrappers -------------------------------------------------------------

export enum ForceMode2D { Force = 0, Impulse = 1 }
export enum ForceMode { Force = 0, Impulse = 1 }
export enum RigidbodyType2D { Static = 0, Kinematic = 1, Dynamic = 2 }

export class Rigidbody2D extends Component {
  static readonly typeNames = ['Rigidbody 2D'] as const;
  get velocity(): Vector2 { return N.rb2(this.gameObject._id, 0) ? new Vector2(F[0], F[1]) : new Vector2(); }
  set velocity(v: Vector2) { N.rb2(this.gameObject._id, 1, v.x, v.y); }
  /** Unity 6 name for velocity. */
  get linearVelocity(): Vector2 { return this.velocity; }
  set linearVelocity(v: Vector2) { this.velocity = v; }
  /** Degrees per second. */
  get angularVelocity(): number { return N.rb2(this.gameObject._id, 4); }
  set angularVelocity(v: number) { N.rb2(this.gameObject._id, 5, v); }
  addForce(force: Vector2, mode: ForceMode2D = ForceMode2D.Force): void { N.rb2(this.gameObject._id, mode === ForceMode2D.Impulse ? 3 : 2, force.x, force.y); }
  addTorque(torque: number, mode: ForceMode2D = ForceMode2D.Force): void { N.rb2(this.gameObject._id, mode === ForceMode2D.Impulse ? 7 : 6, torque); }
  movePosition(position: Vector2): void { N.rb2(this.gameObject._id, 9, position.x, position.y); }
  get mass(): number { return N.rb2(this.gameObject._id, 8); }
  isAwake(): boolean { return !!N.rb2(this.gameObject._id, 10); }
  wakeUp(): void { N.rb2(this.gameObject._id, 11); }
  get gravityScale(): number { return this.get('Gravity Scale'); }
  set gravityScale(v: number) { this.set('Gravity Scale', v); }
  get bodyType(): RigidbodyType2D { return this.get('Body Type'); }
  set bodyType(v: RigidbodyType2D) { this.set('Body Type', v); }
  get isKinematic(): boolean { return this.bodyType === RigidbodyType2D.Kinematic; }
  get simulated(): boolean { return this.enabled; }
  set simulated(v: boolean) { this.enabled = v; }
}

export class Rigidbody extends Component {
  static readonly typeNames = ['Rigidbody 3D'] as const;
  get velocity(): Vector3 { return N.rb3(this.gameObject._id, 0) ? new Vector3(F[0], F[1], F[2]) : new Vector3(); }
  set velocity(v: Vector3) { N.rb3(this.gameObject._id, 1, v.x, v.y, v.z); }
  get linearVelocity(): Vector3 { return this.velocity; }
  set linearVelocity(v: Vector3) { this.velocity = v; }
  /** Degrees per second. */
  get angularVelocity(): Vector3 { return N.rb3(this.gameObject._id, 4) ? new Vector3(F[0], F[1], F[2]) : new Vector3(); }
  set angularVelocity(v: Vector3) { N.rb3(this.gameObject._id, 5, v.x, v.y, v.z); }
  addForce(force: Vector3, mode: ForceMode = ForceMode.Force): void { N.rb3(this.gameObject._id, mode === ForceMode.Impulse ? 3 : 2, force.x, force.y, force.z); }
  addTorque(torque: Vector3, mode: ForceMode = ForceMode.Force): void { N.rb3(this.gameObject._id, mode === ForceMode.Impulse ? 7 : 6, torque.x, torque.y, torque.z); }
  get mass(): number { return N.rb3(this.gameObject._id, 8); }
  get useGravity(): boolean { return this.get('Use Gravity'); }
  set useGravity(v: boolean) { this.set('Use Gravity', v); }
  get isKinematic(): boolean { return this.get('Body Type') === 1; }
}
/** Alias matching the engine's own component name. */
export const Rigidbody3D = Rigidbody;

export class Collider2D extends Component {
  static readonly typeNames: readonly string[] = ['Box Collider 2D', 'Circle Collider 2D', 'Capsule Collider 2D', 'Polygon Collider 2D'];
  get isTrigger(): boolean { return this.get('Is Trigger'); }
  set isTrigger(v: boolean) { this.set('Is Trigger', v); }
  get offset(): Vector2 { return this.getVec2('Offset'); }
  set offset(v: Vector2) { this.set('Offset', v); }
  get attachedRigidbody(): Rigidbody2D | null { return this.gameObject.getComponent(Rigidbody2D); }
}
export class BoxCollider2D extends Collider2D {
  static readonly typeNames = ['Box Collider 2D'];
  get size(): Vector2 { return this.getVec2('Size'); }
  set size(v: Vector2) { this.set('Size', v); }
}
export class CircleCollider2D extends Collider2D {
  static readonly typeNames = ['Circle Collider 2D'];
  get radius(): number { return this.get('Radius'); }
  set radius(v: number) { this.set('Radius', v); }
}
export class Collider extends Component {
  static readonly typeNames: readonly string[] = ['Box Collider 3D', 'Sphere Collider 3D', 'Capsule Collider 3D'];
  get isTrigger(): boolean { return this.get('Is Trigger'); }
  set isTrigger(v: boolean) { this.set('Is Trigger', v); }
  get center(): Vector3 { return this.getVec3('Offset'); }
  set center(v: Vector3) { this.set('Offset', v); }
  get attachedRigidbody(): Rigidbody | null { return this.gameObject.getComponent(Rigidbody); }
}
export class BoxCollider extends Collider {
  static readonly typeNames = ['Box Collider 3D'];
  get size(): Vector3 { return this.getVec3('Size'); }
  set size(v: Vector3) { this.set('Size', v); }
}
export class SphereCollider extends Collider {
  static readonly typeNames = ['Sphere Collider 3D'];
  get radius(): number { return this.get('Radius'); }
  set radius(v: number) { this.set('Radius', v); }
}

export class AudioSource extends Component {
  static readonly typeNames = ['Audio Source'] as const;
  play(): void { N.audio(this.gameObject._id, 0); }
  stop(): void { N.audio(this.gameObject._id, 1); }
  pause(): void { N.audio(this.gameObject._id, 2); }
  unPause(): void { N.audio(this.gameObject._id, 3); }
  get isPlaying(): boolean { return !!N.audio(this.gameObject._id, 5); }
  get volume(): number { return this.get('Volume'); }
  set volume(v: number) { N.audio(this.gameObject._id, 4, v); }
  get loop(): boolean { return this.get('Loop'); }
  set loop(v: boolean) { this.set('Loop', v); }
  get mute(): boolean { return this.get('Mute'); }
  set mute(v: boolean) { this.set('Mute', v); }
  get clip(): AudioClip | null { return this.getAsset('Clip', AudioClip); }
  set clip(v: AudioClip | null) { this.set('Clip', v ? v.guid : ''); }
  /** Fire-and-forget playback of another clip (not tied to this source's volume). */
  playOneShot(clip: AudioClip): void { N.playSound(clip.guid, false); }
  static playClip(clip: AudioClip, loop = false): void { N.playSound(clip.guid, loop); }
  static stopAll(): void { N.stopSounds(); }
}

export class SpriteRenderer extends Component {
  static readonly typeNames = ['Sprite Renderer'] as const;
  get color(): Color { return this.getColor('Color'); }
  set color(v: Color) { this.set('Color', v); }
  get size(): Vector2 { return this.getVec2('Size'); }
  set size(v: Vector2) { this.set('Size', v); }
  get flipX(): boolean { return this.get('Flip X'); }
  set flipX(v: boolean) { this.set('Flip X', v); }
  get flipY(): boolean { return this.get('Flip Y'); }
  set flipY(v: boolean) { this.set('Flip Y', v); }
  get sortingOrder(): number { return this.get('Sort Order'); }
  set sortingOrder(v: number) { this.set('Sort Order', v); }
  get sprite(): Texture | null { return this.getAsset('Texture', Texture); }
  set sprite(v: Texture | null) { this.set('Texture', v ? v.guid : ''); }
}

export class MeshRenderer extends Component {
  static readonly typeNames = ['Mesh Renderer', 'Skinned Mesh Renderer'] as const;
  get sortingOrder(): number { return this.get('Sort Order'); }
  set sortingOrder(v: number) { this.set('Sort Order', v); }
  get materials(): Material[] { return ((this.get('Materials') as string[]) ?? []).filter((g) => g).map((g) => new Material(g)); }
}

export class Camera extends Component {
  static readonly typeNames = ['Camera'] as const;
  get backgroundColor(): Color { return this.getColor('Background'); }
  set backgroundColor(v: Color) { this.set('Background', v); }
  get fieldOfView(): number { return this.get('Fov Degrees'); }
  set fieldOfView(v: number) { this.set('Fov Degrees', v); }
  get zoom(): number { return this.get('Zoom'); }
  set zoom(v: number) { this.set('Zoom', v); }
  get orthographic(): boolean { return this.get('Projection') === 0; }
  /** 0 = top screen, 1 = bottom screen. */
  get screen(): number { return this.get('Screen'); }
}

export class ParticleSystem extends Component {
  static readonly typeNames = ['Particle System'] as const;
  play(): void { this.set('Playing', true); }
  stop(): void { this.set('Playing', false); }
  get isPlaying(): boolean { return this.get('Playing'); }
  get emissionRate(): number { return this.get('Emission Rate'); }
  set emissionRate(v: number) { this.set('Emission Rate', v); }
}

/** A C++ script (GameScripts Behaviour) found by getComponent('ClassName'): its
 *  DUALITY_PROPERTY fields by name. */
export class NativeScript {
  constructor(readonly gameObject: GameObject, readonly slot: number) {}
  get className(): string { return N.scriptName(this.gameObject._id, this.slot) ?? ''; }
  get fieldNames(): string[] { return N.scriptFields(this.gameObject._id, this.slot); }
  get(field: string): any { return N.scriptGet(this.gameObject._id, this.slot, field); }
  set(field: string, value: any): boolean { return N.scriptSet(this.gameObject._id, this.slot, field, normalize(value)); }
  get enabled(): boolean { return N.scriptEnabled(this.gameObject._id, this.slot); }
  set enabled(v: boolean) { N.setScriptEnabled(this.gameObject._id, this.slot, !!v); }
  get transform(): Transform { return this.gameObject.transform; }
}

// --- physics queries / collision info -------------------------------------------------------

/** Passed to onCollisionEnter/Exit: describes the OTHER object. */
export class Collision {
  constructor(readonly gameObject: GameObject) {}
  get transform(): Transform { return this.gameObject.transform; }
  get collider(): Collider | null { return this.gameObject.getComponent(Collider); }
  get rigidbody(): Rigidbody | null { return this.gameObject.getComponent(Rigidbody); }
}
/** Passed to onCollisionEnter2D/Exit2D: describes the OTHER object. */
export class Collision2D {
  constructor(readonly gameObject: GameObject) {}
  get transform(): Transform { return this.gameObject.transform; }
  get collider(): Collider2D | null { return this.gameObject.getComponent(Collider2D); }
  get rigidbody(): Rigidbody2D | null { return this.gameObject.getComponent(Rigidbody2D); }
}

export interface RaycastHit2D { gameObject: GameObject; transform: Transform; collider: Collider2D | null; point: Vector2; normal: Vector2; distance: number }
export interface RaycastHit { gameObject: GameObject; transform: Transform; collider: Collider | null; point: Vector3; normal: Vector3; distance: number }
export interface Ray { origin: Vector3; direction: Vector3 }

export class Physics2D {
  /** Closest 2D collider hit along the ray, or null. Only during Play. */
  static raycast(origin: Vector2, direction: Vector2, distance = 1e9): RaycastHit2D | null {
    const d = direction.normalized;
    const go = GameObject._wrap(N.ray2(currentScene(), origin.x, origin.y, d.x, d.y, distance));
    if (!go) return null;
    return { gameObject: go, transform: go.transform, collider: go.getComponent(Collider2D), point: new Vector2(F[0], F[1]), normal: new Vector2(F[2], F[3]), distance: F[4] };
  }
  /** The 2D collider containing `point`, or null. */
  static overlapPoint(point: Vector2): GameObject | null { return GameObject._wrap(N.point2(currentScene(), point.x, point.y)); }
}

export class Physics {
  static raycast(origin: Vector3, direction: Vector3, maxDistance = 1e9): RaycastHit | null {
    const d = direction.normalized;
    const go = GameObject._wrap(N.ray3(currentScene(), origin.x, origin.y, origin.z, d.x, d.y, d.z, maxDistance));
    if (!go) return null;
    return { gameObject: go, transform: go.transform, collider: go.getComponent(Collider), point: new Vector3(F[0], F[1], F[2]), normal: new Vector3(F[3], F[4], F[5]), distance: F[6] };
  }
  /** Ray from the primary perspective camera of `screen` (0 top, 1 bottom) through a pixel. */
  static screenPointToRay(screen: number, point: Vector2): Ray | null {
    if (!N.screenRay(currentScene(), screen, point.x, point.y)) return null;
    return { origin: new Vector3(F[0], F[1], F[2]), direction: new Vector3(F[3], F[4], F[5]) };
  }
}

// --- scenes -----------------------------------------------------------------------------------

export enum LoadSceneMode { Single = 0, Additive = 1 }

export class SceneManager {
  /** Loads `<Assets>/<path>` (e.g. "Scenes/Level2.scene") after the current frame. */
  static loadScene(path: string, mode: LoadSceneMode = LoadSceneMode.Single): void { N.loadScene(path, mode); }
  static loadSceneAdditive(path: string): void { N.loadScene(path, LoadSceneMode.Additive); }
  static unloadScene(path: string): void { N.unloadScene(path); }
}

// --- Object.Instantiate / Destroy ---------------------------------------------------------------

/** Spawns a prefab asset (or clones a scene GameObject) into the current scene. Its scripts
 *  (TypeScript and C++) Awake immediately. */
export function instantiate(original: Prefab | GameObject, parent?: Transform | GameObject | null, position?: Vector3 | Vector2): GameObject | null {
  const parentId = parent ? (parent instanceof GameObject ? parent._id : parent.gameObject._id) : -1;
  // The position goes to the native side so physics bodies are created at the spawn point.
  const p = position ? [position.x, position.y, (position as Vector3).z ?? 0] : null;
  let id = -1;
  if (original instanceof GameObject) id = p ? N.clone(original._id, parentId, p[0], p[1], p[2]) : N.clone(original._id, parentId);
  else if (original instanceof Asset) id = p ? N.instantiate(currentScene(), original.guid, parentId, p[0], p[1], p[2]) : N.instantiate(currentScene(), original.guid, parentId);
  const go = GameObject._wrap(id);
  if (!go) { Debug.warn('instantiate failed for', original); return null; }
  return go;
}

/** Destroys a GameObject at the end of the frame (after `delay` seconds when given): its scripts
 *  get onDisable/onDestroy and its physics bodies are released. Pass the GameObject -- a script or
 *  component argument is only disabled (runtime component removal isn't supported yet). */
export function destroy(obj: GameObject | Component | MonoBehaviour | null | undefined, delay = 0): void {
  if (!obj) return;
  const go = obj instanceof GameObject ? obj : obj.gameObject;
  if (!(obj instanceof GameObject)) {
    if (obj instanceof MonoBehaviour) { obj.enabled = false; Debug.warn('destroy(script): scripts cannot be removed at runtime yet -- disabled it instead'); }
    else Debug.warn('destroy(component): only GameObjects can be destroyed at runtime');
    return;
  }
  if (delay > 0) { scheduleTimer(slotOf(go._id), delay, () => N.destroy(go._id)); return; }
  N.destroy(go._id);
}

// =============================================================================================
// MonoBehaviour, coroutines, decorators, registration
// =============================================================================================

let pendingId = -1;
let pendingKey = 0;

export class WaitForSeconds { constructor(readonly seconds: number) {} }
export class WaitUntil { constructor(readonly predicate: () => boolean) {} }
export class WaitWhile { constructor(readonly predicate: () => boolean) {} }
export type CoroutineGenerator = Generator<any, any, any>;
export class Coroutine {
  /** @internal */ _done = false;
  /** @internal */ _wakeAt = 0;
  /** @internal frame of the last resume: never resume twice in one frame */ _frame = -1;
  /** @internal */ _wait: any = null;
  constructor(/** @internal */ readonly _owner: MonoBehaviour, /** @internal */ readonly _it: CoroutineGenerator) {}
  get isDone(): boolean { return this._done; }
}

interface Timer { slot: number; at: number; fn: () => void }
const coroutines: Coroutine[] = [];
const timers: Timer[] = [];
function scheduleTimer(slot: number, delay: number, fn: () => void): void { timers.push({ slot, at: N.time() + delay, fn }); }

function stepCoroutine(co: Coroutine, now: number): void {
  if (co._wait instanceof WaitForSeconds) { if (now < co._wakeAt) return; }
  else if (co._wait instanceof WaitUntil) { if (!co._wait.predicate()) return; }
  else if (co._wait instanceof WaitWhile) { if (co._wait.predicate()) return; }
  else if (co._wait instanceof Coroutine) { if (!co._wait._done) return; }
  co._wait = null;
  co._frame = N.frame();
  const r = co._it.next();
  if (r.done) { co._done = true; return; }
  co._wait = r.value ?? null;
  if (co._wait instanceof WaitForSeconds) co._wakeAt = now + co._wait.seconds;
  if (typeof co._wait === 'object' && co._wait !== null && typeof co._wait.next === 'function' && !(co._wait instanceof Coroutine))
    co._wait = co._owner.startCoroutine(co._wait); // nested generator
}

/** Unity's MonoBehaviour. Export a subclass from any .ts file under Assets/ to make it a script.
 *
 *  Lifecycle (same order as C++ Behaviours): awake -> onEnable -> start -> fixedUpdate (per
 *  physics step) -> update -> lateUpdate -> onDisable -> onDestroy; collisions:
 *  onCollisionEnter/Exit(Collision), onTriggerEnter/Exit(Collider) and their 2D variants
 *  (onCollisionEnter2D(Collision2D), onTriggerEnter2D(Collider2D)...). A 2D contact calls the
 *  2D method, falling back to the 3D-named one when the script only defines that. */
export abstract class MonoBehaviour {
  /** @internal */ _id: number;
  /** @internal */ _k: number;
  /** @internal */ _go?: GameObject | null;

  constructor() {
    this._id = pendingId;
    this._k = pendingKey;
  }

  get gameObject(): GameObject { return this._go ?? (this._go = GameObject._wrap(this._id))!; }
  get transform(): Transform { return this.gameObject.transform; }
  get name(): string { return this.gameObject.name; }
  get tag(): string { return this.gameObject.tag; }
  compareTag(tag: string): boolean { return this.gameObject.compareTag(tag); }
  /** MonoBehaviour.enabled -- this script slot only. */
  get enabled(): boolean { return this._k ? N.benabled(this._k) : false; }
  set enabled(value: boolean) { if (this._k) N.bsetEnabled(this._k, !!value); }
  get isActiveAndEnabled(): boolean { return this.enabled && this.gameObject.activeInHierarchy; }
  /** False once this script instance was destroyed (its GameObject destroyed / Play stopped). */
  get isValid(): boolean { return this._k !== 0; }

  getComponent<T>(type: ComponentType<T> | string): T | null { return this.gameObject.getComponent(type); }
  getComponents<T>(type: ComponentType<T> | string): T[] { return this.gameObject.getComponents(type); }
  getComponentInChildren<T>(type: ComponentType<T> | string, includeInactive = false): T | null { return this.gameObject.getComponentInChildren(type, includeInactive); }
  getComponentsInChildren<T>(type: ComponentType<T> | string, includeInactive = false): T[] { return this.gameObject.getComponentsInChildren(type, includeInactive); }
  getComponentInParent<T>(type: ComponentType<T> | string): T | null { return this.gameObject.getComponentInParent(type); }
  addComponent<T>(type: ComponentType<T> | string): T | null { return this.gameObject.addComponent(type); }

  instantiate(original: Prefab | GameObject, parent?: Transform | GameObject | null, position?: Vector3 | Vector2): GameObject | null { return instantiate(original, parent, position); }
  destroy(obj: GameObject | Component | MonoBehaviour | null | undefined, delay = 0): void { destroy(obj, delay); }
  print(...args: any[]): void { N.log(0, join(args)); }

  /** Runs a generator: `yield null` waits a frame, `yield new WaitForSeconds(1)`,
   *  `yield new WaitUntil(() => ...)`, or yield another coroutine. */
  startCoroutine(routine: CoroutineGenerator): Coroutine {
    const co = new Coroutine(this, routine);
    stepCoroutine(co, N.time()); // runs to the first yield right away, like Unity
    if (!co._done) coroutines.push(co);
    return co;
  }
  stopCoroutine(co: Coroutine | null): void { if (co) co._done = true; }
  stopAllCoroutines(): void { for (const co of coroutines) if (co._owner === this) co._done = true; }
  /** Calls `method` (a method name or function) after `delay` seconds. */
  invoke(method: string | (() => void), delay: number): void {
    const self = this;
    this.startCoroutine((function* () { yield new WaitForSeconds(delay); callMethod(self, method); })());
  }
  invokeRepeating(method: string | (() => void), delay: number, rate: number): Coroutine {
    const self = this;
    return this.startCoroutine((function* () { yield new WaitForSeconds(delay); for (;;) { callMethod(self, method); yield new WaitForSeconds(rate); } })());
  }
  cancelInvoke(): void { this.stopAllCoroutines(); }
}

function callMethod(target: any, method: string | (() => void)): void {
  if (typeof method === 'function') method.call(target);
  else if (typeof target[method] === 'function') target[method]();
}

// --- @field decorator ---------------------------------------------------------------------------

type FieldTypeRef = string | (abstract new (...args: any[]) => any) | (new (...args: any[]) => any);
export interface FieldOptions {
  /** 'int' | 'float' | 'bool' | 'string' | 'entity' | 'asset', or a class: Vector2, Vector3, Color,
   *  GameObject, Transform, a component/script class, Prefab, AudioClip, ScriptableObject... */
  type?: FieldTypeRef | [FieldTypeRef];
  /** Slider in the Inspector (Unity's [Range]). */
  range?: [number, number];
  tooltip?: string;
  /** Bold label above the field (Unity's [Header]). */
  header?: string;
  /** Dropdown: a list of labels (value = index) or a numeric TS enum object. */
  options?: readonly string[];
  enum?: Record<string, string | number>;
}

interface FieldMeta { key: string; opts: FieldOptions }

/** Marks a field as an Inspector field (Unity's [SerializeField]). Plain public fields with a
 *  number/string/boolean/Vector/Color default are picked up without it; use @field for slider
 *  ranges, ints, dropdowns and object/asset references:
 *    `@field({ range: [0, 10] }) speed = 2;`  `@field(GameObject) target: GameObject | null = null;`
 *    `@field(Prefab) bullet: Prefab | null = null;`  `@field('int') lives = 3;` */
export function field(options?: FieldOptions | FieldTypeRef | [FieldTypeRef]): (target: any, key: string) => void {
  const opts: FieldOptions = options === undefined ? {} :
    (typeof options === 'string' || typeof options === 'function' || Array.isArray(options)) ? { type: options as any } : options;
  return (target: any, key: string) => {
    const ctor = target.constructor;
    if (!Object.prototype.hasOwnProperty.call(ctor, '__fields')) Object.defineProperty(ctor, '__fields', { value: [], enumerable: false });
    (ctor.__fields as FieldMeta[]).push({ key, opts });
  };
}
/** Unity spelling. */
export const serializeField = field;

/** Excludes an auto-detected public field from the Inspector/serialization. */
export function nonSerialized(target: any, key: string): void {
  const ctor = target.constructor;
  if (!Object.prototype.hasOwnProperty.call(ctor, '__nonSerialized')) Object.defineProperty(ctor, '__nonSerialized', { value: new Set<string>(), enumerable: false });
  ctor.__nonSerialized.add(key);
}

// Field kinds -- MUST match Js::FieldKind in JsInternal.h (order) and ParseFieldKind (names).
const KIND_NAMES = ['float', 'int', 'bool', 'string', 'vec2', 'vec3', 'vec4', 'color', 'asset', 'entity', 'enum', 'assets', 'entities'] as const;
type KindName = typeof KIND_NAMES[number];

interface ResolvedField { name: string; kind: KindName; type: any; def: any; options?: string[]; range?: [number, number]; tooltip?: string; header?: string }

function collectFieldMeta(ctor: any): FieldMeta[] {
  const chain: any[] = [];
  for (let c = ctor; c && c !== MonoBehaviour && c !== Function.prototype; c = Object.getPrototypeOf(c)) chain.unshift(c);
  const out: FieldMeta[] = [];
  for (const c of chain) if (Object.prototype.hasOwnProperty.call(c, '__fields')) out.push(...c.__fields);
  return out;
}
function isNonSerialized(ctor: any, key: string): boolean {
  for (let c = ctor; c && c !== MonoBehaviour; c = Object.getPrototypeOf(c))
    if (Object.prototype.hasOwnProperty.call(c, '__nonSerialized') && c.__nonSerialized.has(key)) return true;
  return false;
}

function kindFromValue(value: any): KindName | null {
  if (typeof value === 'number') return 'float';
  if (typeof value === 'boolean') return 'bool';
  if (typeof value === 'string') return 'string';
  if (value instanceof Vector2) return 'vec2';
  if (value instanceof Vector3) return 'vec3';
  if (value instanceof Vector4) return 'vec4';
  if (value instanceof Color) return 'color';
  if (value instanceof Asset) return 'asset';
  if (value instanceof GameObject) return 'entity';
  return null;
}
function kindFromType(type: any): KindName | null {
  if (typeof type === 'string') {
    const t = type.toLowerCase();
    if (t === 'number' || t === 'float') return 'float';
    if (t === 'int' || t === 'integer') return 'int';
    if (t === 'bool' || t === 'boolean') return 'bool';
    if (t === 'string') return 'string';
    if (t === 'entity' || t === 'gameobject') return 'entity';
    if (t === 'asset') return 'asset';
    return null;
  }
  if (type === Vector2) return 'vec2';
  if (type === Vector3) return 'vec3';
  if (type === Vector4) return 'vec4';
  if (type === Color) return 'color';
  if (type === GameObject || type === Transform || isBuiltinClass(type) || isScriptClass(type)) return 'entity';
  if (type === Asset || (typeof type === 'function' && type.prototype instanceof Asset)) return 'asset';
  return null;
}

function resolveFields(ctor: any, probe: any): ResolvedField[] {
  const fields: ResolvedField[] = [];
  const seen = new Set<string>();
  for (const { key, opts } of collectFieldMeta(ctor)) {
    if (seen.has(key)) continue;
    seen.add(key);
    const value = probe ? probe[key] : undefined;
    let kind: KindName | null = null;
    let type: any = undefined;
    let options: string[] | undefined;
    if (opts.options || opts.enum) {
      kind = 'enum';
      options = opts.options ? [...opts.options] :
        Object.keys(opts.enum!).filter((k) => typeof opts.enum![k] === 'number').sort((a, b) => (opts.enum![a] as number) - (opts.enum![b] as number));
    } else if (Array.isArray(opts.type)) {
      type = opts.type[0];
      const element = kindFromType(type);
      kind = element === 'entity' ? 'entities' : element === 'asset' ? 'assets' : null;
    } else if (opts.type !== undefined) {
      type = opts.type;
      kind = kindFromType(type);
    } else {
      kind = kindFromValue(value);
      if (kind === 'entity') type = GameObject;
      if (kind === 'asset') type = value.constructor;
    }
    if (!kind) { Debug.warn(`[TS] ${ctor.__dualityName ?? ctor.name}.${key}: can't tell the field type -- use @field(Type)`); continue; }
    fields.push({ name: key, kind, type, def: normalizeForKind(kind, value), options, range: opts.range, tooltip: opts.tooltip, header: opts.header });
  }
  if (probe) {
    for (const key of Object.keys(probe)) {
      if (seen.has(key) || key.startsWith('_') || isNonSerialized(ctor, key)) continue;
      const value = probe[key];
      const kind = kindFromValue(value);
      if (!kind || kind === 'entity') continue; // object refs need an explicit @field(Type)
      seen.add(key);
      fields.push({ name: key, kind, type: kind === 'asset' ? value.constructor : undefined, def: normalizeForKind(kind, value) });
    }
  }
  return fields;
}

/** Script value -> the native "normalized" form for a field kind. */
function normalizeForKind(kind: KindName, value: any): any {
  switch (kind) {
    case 'float': case 'int': case 'enum': return typeof value === 'number' ? value : Number(value) || 0;
    case 'bool': return !!value;
    case 'string': return value == null ? '' : String(value);
    case 'vec2': return value ? [value.x ?? value[0] ?? 0, value.y ?? value[1] ?? 0] : [0, 0];
    case 'vec3': return value ? [value.x ?? value[0] ?? 0, value.y ?? value[1] ?? 0, value.z ?? value[2] ?? 0] : [0, 0, 0];
    case 'vec4': return value ? [value.x ?? 0, value.y ?? 0, value.z ?? 0, value.w ?? 0] : [0, 0, 0, 0];
    case 'color': return value ? [value.r ?? 1, value.g ?? 1, value.b ?? 1, value.a ?? 1] : [1, 1, 1, 1];
    case 'asset': return value instanceof Asset ? value.guid : typeof value === 'string' ? value : '';
    case 'entity': return entityIdOf(value);
    case 'assets': return Array.isArray(value) ? value.map((v) => (v instanceof Asset ? v.guid : typeof v === 'string' ? v : '')) : [];
    case 'entities': return Array.isArray(value) ? value.map(entityIdOf) : [];
  }
  return value;
}
function entityIdOf(value: any): number {
  if (value instanceof GameObject) return value._id;
  if (value instanceof Component || value instanceof MonoBehaviour || value instanceof NativeScript) return value.gameObject._id;
  return typeof value === 'number' ? value : -1;
}

/** Native "normalized" value -> script value for a field (vectors, wrappers, assets). */
function denormalize(kind: KindName, type: any, value: any): any {
  switch (kind) {
    case 'vec2': return new Vector2(value[0], value[1]);
    case 'vec3': return new Vector3(value[0], value[1], value[2]);
    case 'vec4': return new Vector4(value[0], value[1], value[2], value[3]);
    case 'color': return new Color(value[0], value[1], value[2], value[3]);
    case 'asset': return value ? new (type && type !== Asset ? type : Asset)(value) : null;
    case 'assets': return (value as string[]).map((g) => (g ? new (type && type !== Asset ? type : Asset)(g) : null));
    case 'entity': return entityValue(type, value);
    case 'entities': return (value as number[]).map((id) => entityValue(type, id));
    default: return value;
  }
}
function entityValue(type: any, id: number): any {
  const go = GameObject._wrap(id);
  if (!go || !type || type === GameObject || type === 'entity' || type === 'gameObject') return go;
  if (type === Transform) return go.transform;
  return go.getComponent(type);
}

// --- registration & native hooks -----------------------------------------------------------

const registered = new Map<string, any>();

/** @internal Called by the generated bundle entry with every script module. */
export function __registerScripts(modules: [string, Record<string, any>][]): void {
  for (const [file, module] of modules) {
    for (const exportName of Object.keys(module)) {
      const ctor = module[exportName];
      if (typeof ctor !== 'function' || !(ctor.prototype instanceof MonoBehaviour)) continue;
      if (registered.has(exportName)) {
        if (registered.get(exportName) !== ctor) Debug.error(`[TS] two script classes are named '${exportName}' -- class names must be unique (${file})`);
        continue;
      }
      registered.set(exportName, ctor);
      Object.defineProperty(ctor, '__dualityName', { value: exportName, enumerable: false });

      // Probe instance: field initializers give the Inspector defaults.
      let probe: any = null;
      pendingId = -1; pendingKey = 0;
      try { probe = new ctor(); } catch (e) { Debug.warn(`[TS] ${exportName}: constructor threw while reading field defaults (${stringify(e)})`); }
      const fields = resolveFields(ctor, probe);
      const meta: Record<string, ResolvedField> = {};
      for (const f of fields) meta[f.name] = f;
      Object.defineProperty(ctor, '__fieldMeta', { value: meta, enumerable: false });
      N.registerClass(exportName, ctor, fields.map((f) => ({
        name: f.name, kind: f.kind, def: f.def, options: f.options, range: f.range, tooltip: f.tooltip, header: f.header,
      })), file);
    }
  }
}

function fieldMetaOf(obj: any, name: string): ResolvedField | undefined { return obj?.constructor?.__fieldMeta?.[name]; }

const contactNames = ['onCollisionEnter', 'onCollisionExit', 'onTriggerEnter', 'onTriggerExit'];

N.init({
  create(ctor: any, id: number, key: number): any {
    pendingId = id; pendingKey = key;
    try { return new ctor(); } finally { pendingId = -1; pendingKey = 0; }
  },
  readField(obj: any, name: string, kindIndex: number): any {
    const kind = KIND_NAMES[kindIndex];
    const value = obj[name];
    return normalizeForKind(kind, value);
  },
  writeField(obj: any, name: string, kindIndex: number, value: any): void {
    const kind = KIND_NAMES[kindIndex];
    const type = fieldMetaOf(obj, name)?.type;
    if (kind === 'entity' && type && type !== GameObject && type !== Transform && GameObject._wrap(value)) {
      // A component/script reference resolves lazily: the target's scripts may not exist yet
      // when overrides are applied (before awake), exactly like Unity resolving on load.
      Object.defineProperty(obj, name, {
        configurable: true, enumerable: true,
        get() { const c = entityValue(type, value); if (c) Object.defineProperty(obj, name, { value: c, writable: true, configurable: true, enumerable: true }); return c; },
        set(v) { Object.defineProperty(obj, name, { value: v, writable: true, configurable: true, enumerable: true }); },
      });
      return;
    }
    obj[name] = denormalize(kind, type, value);
  },
  contact(obj: any, type: number, otherId: number, is2D: boolean): void {
    const go = GameObject._wrap(otherId);
    if (!go) return;
    const base = contactNames[type];
    let fn = is2D ? obj[base + '2D'] : obj[base];
    if (typeof fn !== 'function' && is2D) fn = obj[base];
    if (typeof fn !== 'function') return;
    let arg: any;
    if (type < 2) arg = is2D ? new Collision2D(go) : new Collision(go);
    else arg = (is2D ? go.getComponent(Collider2D) : go.getComponent(Collider)) ?? new Collision(go);
    fn.call(obj, arg);
  },
  sceneStopped(slot: number): void {
    for (const id of [...goCache.keys()]) if (slotOf(id) === slot) goCache.delete(id);
    for (const co of coroutines) if (slotOf(co._owner._id) === slot) co._done = true;
    for (let i = timers.length - 1; i >= 0; i--) if (timers[i].slot === slot) timers.splice(i, 1);
    compact();
  },
  tick(slot: number): void {
    const frame = N.frame();
    // Forget wrappers of destroyed entities now and then (spawn/destroy-heavy games would
    // otherwise grow goCache forever).
    if (frame % 120 === 0 && goCache.size > 0) {
      for (const id of [...goCache.keys()]) if (slotOf(id) === slot && !N.valid(id)) goCache.delete(id);
    }
    if (!coroutines.length && !timers.length) return;
    const now = N.time();
    for (let i = 0; i < timers.length; i++) {
      const t = timers[i];
      if (t.slot === slot && now >= t.at) { timers.splice(i--, 1); t.fn(); }
    }
    const count = coroutines.length;
    for (let i = 0; i < count; i++) {
      const co = coroutines[i];
      if (co._done) continue;
      const owner = co._owner;
      if (!owner._k) { co._done = true; continue; }          // script destroyed
      if (slotOf(owner._id) !== slot) continue;
      if (!N.activeInHierarchy(owner._id)) continue;          // paused while inactive
      if (co._frame === frame) continue;                       // started/resumed this frame
      try { stepCoroutine(co, now); } catch (e) { co._done = true; throw e; }
    }
    compact();
  },
}, F);

function compact(): void {
  let w = 0;
  for (let r = 0; r < coroutines.length; r++) if (!coroutines[r]._done) coroutines[w++] = coroutines[r];
  coroutines.length = w;
}

/** Platform the game runs on. */
export const Application = {
  get platform(): 'desktop' | '3ds' { return N.platform(); },
  /** QuickJS heap usage in bytes (malloc'd by the script runtime). */
  get scriptMemory(): number { N.memory(); return F[0]; },
  /** Forces a full garbage-collection cycle. */
  collectGarbage(): void { N.gc(); },
};
