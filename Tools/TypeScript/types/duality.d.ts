// GENERATED from Tools/TypeScript/duality/duality.ts by build-scripts.mjs -- do not edit.
// The DualityEngine TypeScript API: import { MonoBehaviour, ... } from "duality".
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
export declare class Vector2 {
    x: number;
    y: number;
    constructor(x?: number, y?: number);
    static get zero(): Vector2;
    static get one(): Vector2;
    /** (0, -1): +Y points down in DualityEngine. */
    static get up(): Vector2;
    static get down(): Vector2;
    static get left(): Vector2;
    static get right(): Vector2;
    set(x: number, y: number): this;
    clone(): Vector2;
    copyFrom(v: Vector2): this;
    add(v: Vector2): Vector2;
    sub(v: Vector2): Vector2;
    mul(s: number): Vector2;
    div(s: number): Vector2;
    scale(v: Vector2): Vector2;
    negate(): Vector2;
    dot(v: Vector2): number;
    get sqrMagnitude(): number;
    get magnitude(): number;
    get normalized(): Vector2;
    normalize(): this;
    equals(v: Vector2, epsilon?: number): boolean;
    toVector3(z?: number): Vector3;
    toString(): string;
    static dot(a: Vector2, b: Vector2): number;
    static distance(a: Vector2, b: Vector2): number;
    static lerp(a: Vector2, b: Vector2, t: number): Vector2;
    static moveTowards(current: Vector2, target: Vector2, maxDelta: number): Vector2;
    static clampMagnitude(v: Vector2, max: number): Vector2;
    /** Unsigned angle in degrees between two vectors. */
    static angle(a: Vector2, b: Vector2): number;
}
export declare class Vector3 {
    x: number;
    y: number;
    z: number;
    constructor(x?: number, y?: number, z?: number);
    static get zero(): Vector3;
    static get one(): Vector3;
    /** (0, -1, 0): +Y points down in DualityEngine. */
    static get up(): Vector3;
    static get down(): Vector3;
    static get left(): Vector3;
    static get right(): Vector3;
    static get forward(): Vector3;
    static get back(): Vector3;
    set(x: number, y: number, z: number): this;
    clone(): Vector3;
    copyFrom(v: Vector3): this;
    add(v: Vector3): Vector3;
    sub(v: Vector3): Vector3;
    mul(s: number): Vector3;
    div(s: number): Vector3;
    scale(v: Vector3): Vector3;
    negate(): Vector3;
    dot(v: Vector3): number;
    cross(v: Vector3): Vector3;
    get sqrMagnitude(): number;
    get magnitude(): number;
    get normalized(): Vector3;
    normalize(): this;
    equals(v: Vector3, epsilon?: number): boolean;
    toVector2(): Vector2;
    toString(): string;
    static dot(a: Vector3, b: Vector3): number;
    static cross(a: Vector3, b: Vector3): Vector3;
    static distance(a: Vector3, b: Vector3): number;
    static lerp(a: Vector3, b: Vector3, t: number): Vector3;
    static moveTowards(current: Vector3, target: Vector3, maxDelta: number): Vector3;
    static clampMagnitude(v: Vector3, max: number): Vector3;
    static angle(a: Vector3, b: Vector3): number;
}
export declare class Vector4 {
    x: number;
    y: number;
    z: number;
    w: number;
    constructor(x?: number, y?: number, z?: number, w?: number);
    clone(): Vector4;
    toString(): string;
}
/** Rotation quaternion. Euler angles follow the engine's Rz * Ry * Rx composition (degrees). */
export declare class Quaternion {
    x: number;
    y: number;
    z: number;
    w: number;
    constructor(x?: number, y?: number, z?: number, w?: number);
    static get identity(): Quaternion;
    static euler(x: number, y: number, z: number): Quaternion;
    static angleAxis(angle: number, axis: Vector3): Quaternion;
    get eulerAngles(): Vector3;
    multiply(q: Quaternion): Quaternion;
    /** Rotates a vector by this quaternion. */
    multiplyVector(v: Vector3): Vector3;
    get inverse(): Quaternion;
    static slerp(a: Quaternion, b: Quaternion, t: number): Quaternion;
    toString(): string;
}
/** RGBA color, components 0..1. */
export declare class Color {
    r: number;
    g: number;
    b: number;
    a: number;
    constructor(r?: number, g?: number, b?: number, a?: number);
    static get white(): Color;
    static get black(): Color;
    static get red(): Color;
    static get green(): Color;
    static get blue(): Color;
    static get yellow(): Color;
    static get cyan(): Color;
    static get magenta(): Color;
    static get gray(): Color;
    static get clear(): Color;
    clone(): Color;
    static lerp(a: Color, b: Color, t: number): Color;
    toString(): string;
}
export declare class Mathf {
    static readonly PI: number;
    static readonly Deg2Rad: number;
    static readonly Rad2Deg: number;
    static readonly Epsilon = 1.401298e-45;
    static readonly Infinity: number;
    static clamp(v: number, min: number, max: number): number;
    static clamp01(v: number): number;
    static lerp(a: number, b: number, t: number): number;
    static lerpUnclamped(a: number, b: number, t: number): number;
    static inverseLerp(a: number, b: number, v: number): number;
    static moveTowards(current: number, target: number, maxDelta: number): number;
    static repeat(t: number, length: number): number;
    static pingPong(t: number, length: number): number;
    static deltaAngle(current: number, target: number): number;
    static lerpAngle(a: number, b: number, t: number): number;
    static moveTowardsAngle(current: number, target: number, maxDelta: number): number;
    static approximately(a: number, b: number): boolean;
    static sign(v: number): number;
    static abs: (x: number) => number;
    static min: (...values: number[]) => number;
    static max: (...values: number[]) => number;
    static sqrt: (x: number) => number;
    static pow: (x: number, y: number) => number;
    static sin: (x: number) => number;
    static cos: (x: number) => number;
    static tan: (x: number) => number;
    static atan2: (y: number, x: number) => number;
    static floor: (x: number) => number;
    static ceil: (x: number) => number;
    static floorToInt(v: number): number;
    static ceilToInt(v: number): number;
    static roundToInt(v: number): number;
    static smoothStep(from: number, to: number, t: number): number;
}
export declare class Random {
    /** Float in [0, 1). */
    static get value(): number;
    /** Float in [min, max). */
    static range(min: number, max: number): number;
    /** Integer in [min, max) -- Unity's Random.Range(int, int). */
    static rangeInt(min: number, max: number): number;
    static get insideUnitCircle(): Vector2;
    static get onUnitCircle(): Vector2;
}
/** Logs go to the Editor's Console panel (and stdout / the 3DS debug output). */
export declare class Debug {
    static log(...args: any[]): void;
    static warn(...args: any[]): void;
    static error(...args: any[]): void;
    static logWarning(...args: any[]): void;
    static logError(...args: any[]): void;
    static assert(condition: any, ...args: any[]): void;
}
export declare class Time {
    /** Seconds since last frame (the fixed step inside fixedUpdate). */
    static get deltaTime(): number;
    static get unscaledDeltaTime(): number;
    /** Seconds since Play started. */
    static get time(): number;
    static get frameCount(): number;
    static readonly fixedDeltaTime: number;
    static get inFixedTimeStep(): boolean;
    static get realtimeSinceStartup(): number;
}
/** Keys and 3DS buttons (DualityEngine/Input/KeyCode.h). Desktop reports the keyboard codes,
 *  the 3DS reports the Gamepad* codes. */
export declare const KeyCode: {
    readonly W: 0;
    readonly A: 1;
    readonly S: 2;
    readonly D: 3;
    readonly UpArrow: 4;
    readonly DownArrow: 5;
    readonly LeftArrow: 6;
    readonly RightArrow: 7;
    readonly Space: 8;
    readonly Return: 9;
    readonly Escape: 10;
    readonly GamepadA: 11;
    readonly GamepadB: 12;
    readonly GamepadX: 13;
    readonly GamepadY: 14;
    readonly GamepadL: 15;
    readonly GamepadR: 16;
    readonly GamepadStart: 17;
    readonly GamepadSelect: 18;
    readonly GamepadDPadUp: 19;
    readonly GamepadDPadDown: 20;
    readonly GamepadDPadLeft: 21;
    readonly GamepadDPadRight: 22;
};
export type KeyCode = typeof KeyCode[keyof typeof KeyCode];
export declare class Input {
    static getKey(key: KeyCode | string): boolean;
    static getKeyDown(key: KeyCode | string): boolean;
    static getKeyUp(key: KeyCode | string): boolean;
    /** "Horizontal" / "Vertical" in [-1, 1] (keyboard on desktop, Circle Pad on 3DS; Vertical is +down). */
    static getAxis(name: string): number;
    static getAxisRaw(name: string): number;
    /** "Jump" / "Fire1" / "Fire2" / "Submit" / "Cancel" mapped to keyboard + 3DS buttons. */
    static getButton(name: string): boolean;
    static getButtonDown(name: string): boolean;
    static getButtonUp(name: string): boolean;
    /** Mouse (desktop) / touch screen (3DS). Only button 0 exists. */
    static getMouseButton(button?: number): boolean;
    static getMouseButtonDown(button?: number): boolean;
    static getMouseButtonUp(button?: number): boolean;
    /** Pointer position in the local pixels of `pointerScreen` (top-left origin, Y down). */
    static get mousePosition(): Vector3;
    /** 0 = top screen, 1 = bottom (touch) screen. */
    static get pointerScreen(): number;
    static get touchCount(): number;
}
/** A reference to a project asset by its .meta GUID (what an asset field stores). */
export declare class Asset {
    readonly guid: string;
    constructor(guid: string);
    /** Project path the GUID resolves to ("" if unknown). */
    get path(): string;
    toString(): string;
}
export declare class Prefab extends Asset {
}
export declare class AudioClip extends Asset {
}
export declare class Texture extends Asset {
}
export declare class Material extends Asset {
}
export declare class SceneAsset extends Asset {
}
/** Names of the built-in meshes (Unity's GameObject > 3D Object primitives). */
export type BuiltinMeshName = 'Cube' | 'Sphere' | 'Capsule' | 'Cylinder' | 'Plane' | 'Quad';
/** A mesh asset: an imported mesh/model GUID or a built-in mesh ("builtin:Cube"). */
export declare class Mesh extends Asset {
    /** The built-in mesh `name` (`Mesh.builtin('Sphere')`), like Unity's built-in Sphere mesh. */
    static builtin(name: BuiltinMeshName): Mesh;
    /** True for one of the built-in meshes. */
    get isBuiltin(): boolean;
    /** "Cube" for a built-in mesh, else the asset's file name. */
    get name(): string;
}
/** A ScriptableObject data asset (.asset, class defined in C++ GameScripts): fields by name. */
export declare class ScriptableObject extends Asset {
    get className(): string;
    get fieldNames(): string[];
    get(field: string): any;
    set(field: string, value: any): boolean;
    static load(guid: string): ScriptableObject | null;
}
/** Unity's GameObject: an entity in a scene. Wrappers are cached, so `===` compares identity. */
export declare class GameObject {
    /** Creates a new, empty entity in the current scene. */
    constructor(name?: string);
    get name(): string;
    set name(value: string);
    get tag(): string;
    set tag(value: string);
    compareTag(tag: string): boolean;
    get activeSelf(): boolean;
    get activeInHierarchy(): boolean;
    setActive(value: boolean): void;
    /** False once the entity was destroyed (Unity's `obj == null`). */
    get isValid(): boolean;
    get destroyed(): boolean;
    get transform(): Transform;
    getComponent<T>(type: ComponentType<T> | string): T | null;
    getComponents<T>(type: ComponentType<T> | string): T[];
    getComponentInChildren<T>(type: ComponentType<T> | string, includeInactive?: boolean): T | null;
    getComponentsInChildren<T>(type: ComponentType<T> | string, includeInactive?: boolean): T[];
    getComponentInParent<T>(type: ComponentType<T> | string): T | null;
    /** Adds a built-in component (by class or Inspector name) or a script (TS class / C++ class
     *  name). During Play a script is created and Awakes immediately. */
    addComponent<T>(type: ComponentType<T> | string): T | null;
    /** Calls `method()` on every TypeScript script of this GameObject (Unity's SendMessage). */
    sendMessage(method: string): void;
    static find(name: string): GameObject | null;
    static findWithTag(tag: string): GameObject | null;
    static findGameObjectsWithTag(tag: string): GameObject[];
    toString(): string;
}
/** A built-in component class (Rigidbody2D, SpriteRenderer, ...), a script class, or a name. */
export type ComponentType<T> = (abstract new (...args: any[]) => T) | (new (...args: any[]) => T);
/** Base of the built-in component wrappers. Any Inspector field is reachable by its Inspector
 *  name through get()/set(), e.g. `getComponent('Particle System').set('Emission Rate', 20)`. */
export declare class Component {
    readonly gameObject: GameObject;
    readonly typeName: string;
    /** Inspector names of the C++ component types this wrapper class stands for. */
    static readonly typeNames: readonly string[];
    constructor(gameObject: GameObject, typeName: string);
    get transform(): Transform;
    get name(): string;
    /** Reads a field by its Inspector name (vectors/colors come back as arrays). */
    get(field: string): any;
    set(field: string, value: any): boolean;
    get enabled(): boolean;
    set enabled(value: boolean);
    protected getVec2(field: string): Vector2;
    protected getVec3(field: string): Vector3;
    protected getColor(field: string): Color;
    protected getAsset<T extends Asset>(field: string, type: new (guid: string) => T): T | null;
    toString(): string;
}
export declare enum Space {
    World = 0,
    Self = 1
}
export declare class Transform extends Component {
    static readonly typeNames: readonly ["Transform"];
    get position(): Vector3;
    set position(v: Vector3);
    get localPosition(): Vector3;
    set localPosition(v: Vector3);
    /** World Euler angles in degrees. */
    get eulerAngles(): Vector3;
    set eulerAngles(v: Vector3);
    get localEulerAngles(): Vector3;
    set localEulerAngles(v: Vector3);
    get rotation(): Quaternion;
    set rotation(q: Quaternion);
    get localRotation(): Quaternion;
    set localRotation(q: Quaternion);
    get localScale(): Vector3;
    set localScale(v: Vector3);
    get lossyScale(): Vector3;
    /** Allocation-free accessors for hot paths: write into `out`. */
    getPosition(out: Vector3): Vector3;
    getLocalPosition(out: Vector3): Vector3;
    setPosition(x: number, y: number, z: number): void;
    setLocalPosition(x: number, y: number, z: number): void;
    get forward(): Vector3;
    get right(): Vector3;
    get up(): Vector3;
    /** Moves by (x, y, z) in local-parent space (Space.World also adds directly: the engine's
     *  hierarchy is 2D-affine, so for root objects both are identical). */
    translate(x: number | Vector3 | Vector2, y?: number, z?: number): void;
    /** Adds Euler degrees to the local rotation (2D: rotate(0, 0, degrees)). */
    rotate(x: number | Vector3, y?: number, z?: number): void;
    get parent(): Transform | null;
    set parent(value: Transform | null);
    setParent(parent: Transform | GameObject | null, worldPositionStays?: boolean): void;
    get root(): Transform;
    get childCount(): number;
    getChild(index: number): Transform | null;
    /** Direct child by name ("a/b" paths walk down). */
    find(path: string): Transform | null;
}
export declare enum ForceMode2D {
    Force = 0,
    Impulse = 1
}
export declare enum ForceMode {
    Force = 0,
    Impulse = 1
}
export declare enum RigidbodyType2D {
    Static = 0,
    Kinematic = 1,
    Dynamic = 2
}
export declare class Rigidbody2D extends Component {
    static readonly typeNames: readonly ["Rigidbody 2D"];
    get velocity(): Vector2;
    set velocity(v: Vector2);
    /** Unity 6 name for velocity. */
    get linearVelocity(): Vector2;
    set linearVelocity(v: Vector2);
    /** Degrees per second. */
    get angularVelocity(): number;
    set angularVelocity(v: number);
    addForce(force: Vector2, mode?: ForceMode2D): void;
    addTorque(torque: number, mode?: ForceMode2D): void;
    movePosition(position: Vector2): void;
    get mass(): number;
    isAwake(): boolean;
    wakeUp(): void;
    get gravityScale(): number;
    set gravityScale(v: number);
    get bodyType(): RigidbodyType2D;
    set bodyType(v: RigidbodyType2D);
    get isKinematic(): boolean;
    get simulated(): boolean;
    set simulated(v: boolean);
}
export declare class Rigidbody extends Component {
    static readonly typeNames: readonly ["Rigidbody 3D"];
    get velocity(): Vector3;
    set velocity(v: Vector3);
    get linearVelocity(): Vector3;
    set linearVelocity(v: Vector3);
    /** Degrees per second. */
    get angularVelocity(): Vector3;
    set angularVelocity(v: Vector3);
    addForce(force: Vector3, mode?: ForceMode): void;
    addTorque(torque: Vector3, mode?: ForceMode): void;
    get mass(): number;
    get useGravity(): boolean;
    set useGravity(v: boolean);
    get isKinematic(): boolean;
}
/** Alias matching the engine's own component name. */
export declare const Rigidbody3D: typeof Rigidbody;
export declare class Collider2D extends Component {
    static readonly typeNames: readonly string[];
    /** Shape of an Effector2D on the same GameObject. */
    get usedByEffector(): boolean;
    set usedByEffector(v: boolean);
    get isTrigger(): boolean;
    set isTrigger(v: boolean);
    get offset(): Vector2;
    set offset(v: Vector2);
    get attachedRigidbody(): Rigidbody2D | null;
}
export declare class BoxCollider2D extends Collider2D {
    static readonly typeNames: string[];
    get size(): Vector2;
    set size(v: Vector2);
}
export declare class CircleCollider2D extends Collider2D {
    static readonly typeNames: string[];
    get radius(): number;
    set radius(v: number);
}
export declare class Collider extends Component {
    static readonly typeNames: readonly string[];
    get isTrigger(): boolean;
    set isTrigger(v: boolean);
    get center(): Vector3;
    set center(v: Vector3);
    get attachedRigidbody(): Rigidbody | null;
}
export declare class BoxCollider extends Collider {
    static readonly typeNames: string[];
    get size(): Vector3;
    set size(v: Vector3);
}
export declare class SphereCollider extends Collider {
    static readonly typeNames: string[];
    get radius(): number;
    set radius(v: number);
}
export declare class AudioSource extends Component {
    static readonly typeNames: readonly ["Audio Source"];
    play(): void;
    stop(): void;
    pause(): void;
    unPause(): void;
    get isPlaying(): boolean;
    get volume(): number;
    set volume(v: number);
    get loop(): boolean;
    set loop(v: boolean);
    get mute(): boolean;
    set mute(v: boolean);
    get clip(): AudioClip | null;
    set clip(v: AudioClip | null);
    /** Fire-and-forget playback of another clip (not tied to this source's volume). */
    playOneShot(clip: AudioClip): void;
    static playClip(clip: AudioClip, loop?: boolean): void;
    static stopAll(): void;
}
export declare class SpriteRenderer extends Component {
    static readonly typeNames: readonly ["Sprite Renderer"];
    get color(): Color;
    set color(v: Color);
    get size(): Vector2;
    set size(v: Vector2);
    get flipX(): boolean;
    set flipX(v: boolean);
    get flipY(): boolean;
    set flipY(v: boolean);
    get sortingOrder(): number;
    set sortingOrder(v: number);
    get sprite(): Texture | null;
    set sprite(v: Texture | null);
}
/** Unity's MeshFilter: which mesh the MeshRenderer of the same GameObject draws. */
export declare class MeshFilter extends Component {
    static readonly typeNames: readonly ["Mesh Filter"];
    /** The mesh (null = none: nothing is drawn). `mesh = Mesh.builtin('Sphere')` switches shape. */
    get mesh(): Mesh | null;
    set mesh(v: Mesh | null);
    /** Unity name; the same asset as `mesh` here (meshes are never instanced per renderer). */
    get sharedMesh(): Mesh | null;
    set sharedMesh(v: Mesh | null);
}
export declare class MeshRenderer extends Component {
    static readonly typeNames: readonly ["Mesh Renderer", "Skinned Mesh Renderer"];
    get sortingOrder(): number;
    set sortingOrder(v: number);
    get materials(): Material[];
    set materials(v: Material[]);
    get material(): Material | null;
    set material(v: Material | null);
}
export declare class Camera extends Component {
    static readonly typeNames: readonly ["Camera"];
    get backgroundColor(): Color;
    set backgroundColor(v: Color);
    get fieldOfView(): number;
    set fieldOfView(v: number);
    get zoom(): number;
    set zoom(v: number);
    get orthographic(): boolean;
    /** 0 = top screen, 1 = bottom screen. */
    get screen(): number;
}
export declare class ParticleSystem extends Component {
    static readonly typeNames: readonly ["Particle System"];
    play(): void;
    stop(): void;
    get isPlaying(): boolean;
    get emissionRate(): number;
    set emissionRate(v: number);
}
export declare class EdgeCollider2D extends Collider2D {
    static readonly typeNames: string[];
    /** Local points of the polyline (2..8). */
    get points(): Vector2[];
    set points(v: Vector2[]);
    get edgeRadius(): number;
    set edgeRadius(v: number);
}
/** Base of every 2D joint: Anchor/Connected Anchor/Connected Body/Break Force (0 = infinity). */
export declare class Joint2D extends Component {
    static readonly typeNames: readonly string[];
    get connectedBody(): GameObject | null;
    set connectedBody(v: GameObject | Rigidbody2D | null);
    get anchor(): Vector2;
    set anchor(v: Vector2);
    get connectedAnchor(): Vector2;
    set connectedAnchor(v: Vector2);
    get autoConfigureConnectedAnchor(): boolean;
    set autoConfigureConnectedAnchor(v: boolean);
    get enableCollision(): boolean;
    set enableCollision(v: boolean);
    /** 0 = Infinity. */
    get breakForce(): number;
    set breakForce(v: number);
    get breakTorque(): number;
    set breakTorque(v: number);
}
export declare class DistanceJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get distance(): number;
    set distance(v: number);
    get maxDistanceOnly(): boolean;
    set maxDistanceOnly(v: boolean);
}
export declare class SpringJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get distance(): number;
    set distance(v: number);
    get dampingRatio(): number;
    set dampingRatio(v: number);
    get frequency(): number;
    set frequency(v: number);
}
export declare class FixedJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get dampingRatio(): number;
    set dampingRatio(v: number);
    get frequency(): number;
    set frequency(v: number);
}
/** Unity's JointMotor2D (speed in degrees/second for hinges and wheels, units/second for sliders). */
export interface JointMotor2D {
    motorSpeed: number;
    maxMotorTorque: number;
}
export interface JointAngleLimits2D {
    min: number;
    max: number;
}
export declare class HingeJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get useMotor(): boolean;
    set useMotor(v: boolean);
    get motor(): JointMotor2D;
    set motor(v: JointMotor2D);
    get useLimits(): boolean;
    set useLimits(v: boolean);
    get limits(): JointAngleLimits2D;
    set limits(v: JointAngleLimits2D);
}
export declare class SliderJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get angle(): number;
    set angle(v: number);
    get useMotor(): boolean;
    set useMotor(v: boolean);
    get motorSpeed(): number;
    set motorSpeed(v: number);
    get useLimits(): boolean;
    set useLimits(v: boolean);
    get limits(): JointAngleLimits2D;
    set limits(v: JointAngleLimits2D);
}
export declare class WheelJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get useMotor(): boolean;
    set useMotor(v: boolean);
    get motor(): JointMotor2D;
    set motor(v: JointMotor2D);
}
export declare class FrictionJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get maxForce(): number;
    set maxForce(v: number);
    get maxTorque(): number;
    set maxTorque(v: number);
}
export declare class RelativeJoint2D extends Joint2D {
    static readonly typeNames: string[];
    get linearOffset(): Vector2;
    set linearOffset(v: Vector2);
    get angularOffset(): number;
    set angularOffset(v: number);
}
export declare class TargetJoint2D extends Joint2D {
    static readonly typeNames: string[];
    /** World point the anchor is pulled to (e.g. the pointer for dragging). */
    get target(): Vector2;
    set target(v: Vector2);
}
/** Passed to onJointBreak2D: the joint is already removed from its GameObject. */
export declare class BrokenJoint2D {
    readonly gameObject: GameObject;
    readonly breakForce: number;
    constructor(gameObject: GameObject, breakForce: number);
    get transform(): Transform;
}
export declare enum EffectorSelection2D {
    Rigidbody = 0,
    Collider = 1
}
export declare enum EffectorForceMode2D {
    Constant = 0,
    InverseLinear = 1,
    InverseSquared = 2
}
export declare class Effector2D extends Component {
    static readonly typeNames: readonly string[];
    get useColliderMask(): boolean;
    set useColliderMask(v: boolean);
    get colliderMask(): number;
    set colliderMask(v: number);
}
export declare class AreaEffector2D extends Effector2D {
    static readonly typeNames: string[];
    /** Degrees, 0 = +X, 90 = up (-Y). */
    get forceAngle(): number;
    set forceAngle(v: number);
    get forceMagnitude(): number;
    set forceMagnitude(v: number);
    get drag(): number;
    set drag(v: number);
}
export declare class PointEffector2D extends Effector2D {
    static readonly typeNames: string[];
    /** Negative attracts, positive repels. */
    get forceMagnitude(): number;
    set forceMagnitude(v: number);
    get forceMode(): EffectorForceMode2D;
    set forceMode(v: EffectorForceMode2D);
}
export declare class SurfaceEffector2D extends Effector2D {
    static readonly typeNames: string[];
    get speed(): number;
    set speed(v: number);
}
export declare class PlatformEffector2D extends Effector2D {
    static readonly typeNames: string[];
    get useOneWay(): boolean;
    set useOneWay(v: boolean);
    get surfaceArc(): number;
    set surfaceArc(v: number);
    get rotationalOffset(): number;
    set rotationalOffset(v: number);
}
export declare class BuoyancyEffector2D extends Effector2D {
    static readonly typeNames: string[];
    get surfaceLevel(): number;
    set surfaceLevel(v: number);
    get density(): number;
    set density(v: number);
    get flowMagnitude(): number;
    set flowMagnitude(v: number);
}
export declare class ConstantForce2D extends Component {
    static readonly typeNames: readonly ["Constant Force 2D"];
    get force(): Vector2;
    set force(v: Vector2);
    get relativeForce(): Vector2;
    set relativeForce(v: Vector2);
    get torque(): number;
    set torque(v: number);
}
export declare class MeshCollider extends Collider {
    static readonly typeNames: string[];
    get convex(): boolean;
    set convex(v: boolean);
}
/** Base of every 3D joint (Break Force/Torque 0 = infinity). */
export declare class Joint extends Component {
    static readonly typeNames: readonly string[];
    get connectedBody(): GameObject | null;
    set connectedBody(v: GameObject | Rigidbody | null);
    get anchor(): Vector3;
    set anchor(v: Vector3);
    get connectedAnchor(): Vector3;
    set connectedAnchor(v: Vector3);
    get breakForce(): number;
    set breakForce(v: number);
    get breakTorque(): number;
    set breakTorque(v: number);
}
export declare class FixedJoint extends Joint {
    static readonly typeNames: string[];
}
export interface JointMotor {
    targetVelocity: number;
    force: number;
    freeSpin: boolean;
}
export interface JointSpring {
    spring: number;
    damper: number;
    targetPosition: number;
}
export interface JointLimits {
    min: number;
    max: number;
    bounciness: number;
}
export declare class HingeJoint extends Joint {
    static readonly typeNames: string[];
    get axis(): Vector3;
    set axis(v: Vector3);
    get useMotor(): boolean;
    set useMotor(v: boolean);
    get motor(): JointMotor;
    set motor(v: JointMotor);
    get useSpring(): boolean;
    set useSpring(v: boolean);
    get spring(): JointSpring;
    set spring(v: JointSpring);
    get useLimits(): boolean;
    set useLimits(v: boolean);
    get limits(): JointLimits;
    set limits(v: JointLimits);
}
export declare class SpringJoint extends Joint {
    static readonly typeNames: string[];
    get spring(): number;
    set spring(v: number);
    get damper(): number;
    set damper(v: number);
    get minDistance(): number;
    set minDistance(v: number);
    get maxDistance(): number;
    set maxDistance(v: number);
}
export declare class CharacterJoint extends Joint {
    static readonly typeNames: string[];
}
export declare class ConstantForce extends Component {
    static readonly typeNames: readonly ["Constant Force"];
    get force(): Vector3;
    set force(v: Vector3);
    get relativeForce(): Vector3;
    set relativeForce(v: Vector3);
    get torque(): Vector3;
    set torque(v: Vector3);
}
/** Unity's CollisionFlags (bit mask returned by CharacterController.move). */
export declare enum CollisionFlags {
    None = 0,
    Sides = 1,
    Above = 2,
    Below = 4
}
export declare class CharacterController extends Component {
    static readonly typeNames: readonly ["Character Controller"];
    /** Moves by `motion` (not scaled by deltaTime), colliding and sliding. */
    move(motion: Vector3): CollisionFlags;
    /** Moves with `speed` units/second (vertical ignored) plus gravity; call once per frame. */
    simpleMove(speed: Vector3): boolean;
    get isGrounded(): boolean;
    get velocity(): Vector3;
    get collisionFlags(): CollisionFlags;
    get radius(): number;
    set radius(v: number);
    get height(): number;
    set height(v: number);
    get center(): Vector3;
    set center(v: Vector3);
    get slopeLimit(): number;
    set slopeLimit(v: number);
    get stepOffset(): number;
    set stepOffset(v: number);
}
/** Passed to onControllerColliderHit. */
export declare class ControllerColliderHit {
    readonly controller: CharacterController;
    readonly gameObject: GameObject | null;
    readonly point: Vector3;
    readonly normal: Vector3;
    readonly moveDirection: Vector3;
    readonly moveLength: number;
    constructor(controller: CharacterController, gameObject: GameObject | null, point: Vector3, normal: Vector3, moveDirection: Vector3, moveLength: number);
    get transform(): Transform | null;
    get collider(): Collider | null;
    get rigidbody(): Rigidbody | null;
}
export declare class ScrollRect extends Component {
    static readonly typeNames: readonly ["UI Scroll Rect"];
    get content(): GameObject | null;
    set content(v: GameObject | null);
    get horizontal(): boolean;
    set horizontal(v: boolean);
    get vertical(): boolean;
    set vertical(v: boolean);
}
export declare class Scrollbar extends Component {
    static readonly typeNames: readonly ["UI Scrollbar"];
    get value(): number;
    set value(v: number);
    get size(): number;
    set size(v: number);
}
export declare class Dropdown extends Component {
    static readonly typeNames: readonly ["UI Dropdown"];
    get value(): number;
    set value(v: number);
    get options(): string[];
    set options(v: string[]);
    get captionText(): string;
}
export declare class CanvasGroup extends Component {
    static readonly typeNames: readonly ["Canvas Group"];
    get alpha(): number;
    set alpha(v: number);
    get interactable(): boolean;
    set interactable(v: boolean);
    get blocksRaycasts(): boolean;
    set blocksRaycasts(v: boolean);
}
export declare class Mask extends Component {
    static readonly typeNames: readonly ["UI Mask", "UI Rect Mask 2D"];
}
export declare class TrailRenderer extends Component {
    static readonly typeNames: readonly ["Trail Renderer"];
    get time(): number;
    set time(v: number);
    get emitting(): boolean;
    set emitting(v: boolean);
    get startWidth(): number;
    set startWidth(v: number);
    get endWidth(): number;
    set endWidth(v: number);
    get startColor(): Color;
    set startColor(v: Color);
    get endColor(): Color;
    set endColor(v: Color);
}
export declare class TextMesh extends Component {
    static readonly typeNames: readonly ["Text Mesh"];
    get text(): string;
    set text(v: string);
    get color(): Color;
    set color(v: Color);
    get fontSize(): number;
    set fontSize(v: number);
    get characterSize(): number;
    set characterSize(v: number);
}
export declare class LODGroup extends Component {
    static readonly typeNames: readonly ["LOD Group"];
    get lodBias(): number;
    set lodBias(v: number);
}
/** Position/Rotation/Scale/Parent/LookAt/Aim Constraint: weight + constraintActive (= enabled). */
export declare class Constraint extends Component {
    static readonly typeNames: readonly string[];
    get weight(): number;
    set weight(v: number);
    get constraintActive(): boolean;
    set constraintActive(v: boolean);
}
export declare class PositionConstraint extends Constraint {
    static readonly typeNames: string[];
}
export declare class RotationConstraint extends Constraint {
    static readonly typeNames: string[];
}
export declare class ScaleConstraint extends Constraint {
    static readonly typeNames: string[];
}
export declare class ParentConstraint extends Constraint {
    static readonly typeNames: string[];
}
export declare class LookAtConstraint extends Constraint {
    static readonly typeNames: string[];
}
export declare class AimConstraint extends Constraint {
    static readonly typeNames: string[];
}
/** A C++ script (GameScripts Behaviour) found by getComponent('ClassName'): its
 *  DUALITY_PROPERTY fields by name. */
export declare class NativeScript {
    readonly gameObject: GameObject;
    readonly slot: number;
    constructor(gameObject: GameObject, slot: number);
    get className(): string;
    get fieldNames(): string[];
    get(field: string): any;
    set(field: string, value: any): boolean;
    get enabled(): boolean;
    set enabled(v: boolean);
    get transform(): Transform;
}
/** Passed to onCollisionEnter/Exit: describes the OTHER object. */
export declare class Collision {
    readonly gameObject: GameObject;
    constructor(gameObject: GameObject);
    get transform(): Transform;
    get collider(): Collider | null;
    get rigidbody(): Rigidbody | null;
}
/** Passed to onCollisionEnter2D/Exit2D: describes the OTHER object. */
export declare class Collision2D {
    readonly gameObject: GameObject;
    constructor(gameObject: GameObject);
    get transform(): Transform;
    get collider(): Collider2D | null;
    get rigidbody(): Rigidbody2D | null;
}
export interface RaycastHit2D {
    gameObject: GameObject;
    transform: Transform;
    collider: Collider2D | null;
    point: Vector2;
    normal: Vector2;
    distance: number;
}
export interface RaycastHit {
    gameObject: GameObject;
    transform: Transform;
    collider: Collider | null;
    point: Vector3;
    normal: Vector3;
    distance: number;
}
export interface Ray {
    origin: Vector3;
    direction: Vector3;
}
export declare class Physics2D {
    /** Closest 2D collider hit along the ray, or null. Only during Play. */
    static raycast(origin: Vector2, direction: Vector2, distance?: number): RaycastHit2D | null;
    /** The 2D collider containing `point`, or null. */
    static overlapPoint(point: Vector2): GameObject | null;
}
export declare class Physics {
    static raycast(origin: Vector3, direction: Vector3, maxDistance?: number): RaycastHit | null;
    /** Ray from the primary perspective camera of `screen` (0 top, 1 bottom) through a pixel. */
    static screenPointToRay(screen: number, point: Vector2): Ray | null;
}
export declare enum LoadSceneMode {
    Single = 0,
    Additive = 1
}
export declare class SceneManager {
    /** Loads `<Assets>/<path>` (e.g. "Scenes/Level2.scene") after the current frame. */
    static loadScene(path: string, mode?: LoadSceneMode): void;
    static loadSceneAdditive(path: string): void;
    static unloadScene(path: string): void;
}
/** Spawns a prefab asset (or clones a scene GameObject) into the current scene. Its scripts
 *  (TypeScript and C++) Awake immediately. */
export declare function instantiate(original: Prefab | GameObject, parent?: Transform | GameObject | null, position?: Vector3 | Vector2): GameObject | null;
/** Destroys a GameObject at the end of the frame (after `delay` seconds when given): its scripts
 *  get onDisable/onDestroy and its physics bodies are released. Pass the GameObject -- a script or
 *  component argument is only disabled (runtime component removal isn't supported yet). */
export declare function destroy(obj: GameObject | Component | MonoBehaviour | null | undefined, delay?: number): void;
export declare class WaitForSeconds {
    readonly seconds: number;
    constructor(seconds: number);
}
export declare class WaitUntil {
    readonly predicate: () => boolean;
    constructor(predicate: () => boolean);
}
export declare class WaitWhile {
    readonly predicate: () => boolean;
    constructor(predicate: () => boolean);
}
export type CoroutineGenerator = Generator<any, any, any>;
export declare class Coroutine {
    constructor(/** @internal */ _owner: MonoBehaviour, /** @internal */ _it: CoroutineGenerator);
    get isDone(): boolean;
}
/** Unity's MonoBehaviour. Export a subclass from any .ts file under Assets/ to make it a script.
 *
 *  Lifecycle (same order as C++ Behaviours): awake -> onEnable -> start -> fixedUpdate (per
 *  physics step) -> update -> lateUpdate -> onDisable -> onDestroy; collisions:
 *  onCollisionEnter/Exit(Collision), onTriggerEnter/Exit(Collider) and their 2D variants
 *  (onCollisionEnter2D(Collision2D), onTriggerEnter2D(Collider2D)...). A 2D contact calls the
 *  2D method, falling back to the 3D-named one when the script only defines that. */
export declare abstract class MonoBehaviour {
    constructor();
    get gameObject(): GameObject;
    get transform(): Transform;
    get name(): string;
    get tag(): string;
    compareTag(tag: string): boolean;
    /** MonoBehaviour.enabled -- this script slot only. */
    get enabled(): boolean;
    set enabled(value: boolean);
    get isActiveAndEnabled(): boolean;
    /** False once this script instance was destroyed (its GameObject destroyed / Play stopped). */
    get isValid(): boolean;
    getComponent<T>(type: ComponentType<T> | string): T | null;
    getComponents<T>(type: ComponentType<T> | string): T[];
    getComponentInChildren<T>(type: ComponentType<T> | string, includeInactive?: boolean): T | null;
    getComponentsInChildren<T>(type: ComponentType<T> | string, includeInactive?: boolean): T[];
    getComponentInParent<T>(type: ComponentType<T> | string): T | null;
    addComponent<T>(type: ComponentType<T> | string): T | null;
    instantiate(original: Prefab | GameObject, parent?: Transform | GameObject | null, position?: Vector3 | Vector2): GameObject | null;
    destroy(obj: GameObject | Component | MonoBehaviour | null | undefined, delay?: number): void;
    print(...args: any[]): void;
    /** Runs a generator: `yield null` waits a frame, `yield new WaitForSeconds(1)`,
     *  `yield new WaitUntil(() => ...)`, or yield another coroutine. */
    startCoroutine(routine: CoroutineGenerator): Coroutine;
    stopCoroutine(co: Coroutine | null): void;
    stopAllCoroutines(): void;
    /** Calls `method` (a method name or function) after `delay` seconds. */
    invoke(method: string | (() => void), delay: number): void;
    invokeRepeating(method: string | (() => void), delay: number, rate: number): Coroutine;
    cancelInvoke(): void;
}
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
/** Marks a field as an Inspector field (Unity's [SerializeField]). Plain public fields with a
 *  number/string/boolean/Vector/Color default are picked up without it; use @field for slider
 *  ranges, ints, dropdowns and object/asset references:
 *    `@field({ range: [0, 10] }) speed = 2;`  `@field(GameObject) target: GameObject | null = null;`
 *    `@field(Prefab) bullet: Prefab | null = null;`  `@field('int') lives = 3;` */
export declare function field(options?: FieldOptions | FieldTypeRef | [FieldTypeRef]): (target: any, key: string) => void;
/** Unity spelling. */
export declare const serializeField: typeof field;
/** Excludes an auto-detected public field from the Inspector/serialization. */
export declare function nonSerialized(target: any, key: string): void;
/** Platform the game runs on. */
export declare const Application: {
    readonly platform: "desktop" | "3ds";
    /** QuickJS heap usage in bytes (malloc'd by the script runtime). */
    readonly scriptMemory: number;
    /** Forces a full garbage-collection cycle. */
    collectGarbage(): void;
};
export {};
