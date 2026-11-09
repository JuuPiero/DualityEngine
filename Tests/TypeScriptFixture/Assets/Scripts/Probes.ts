// Test fixture for Tests/JsScriptingTests.cpp -- bundled at build time by
// Tools/TypeScript/build-scripts.mjs (see Tests/CMakeLists.txt). Each probe records what it saw
// into globalThis.__log, which the C++ tests read back with JsScripting::Eval.
import {
  Collider2D, Collision2D, Color, GameObject, MonoBehaviour, NativeScript, Prefab, Rigidbody2D, ScriptableObject, Time,
  Vector2, Vector3, WaitForSeconds, destroy, field, instantiate,
} from 'duality';

const g = globalThis as any;
g.__log = g.__log ?? [];
function log(entry: string): void { g.__log.push(entry); }

export class LifecycleProbe extends MonoBehaviour {
  private _frames = 0;
  private _fixed = 0;
  awake() { log(`awake:${this.gameObject.name}`); }
  onEnable() { log('onEnable'); }
  start() { log('start'); }
  fixedUpdate() { if (this._fixed++ < 1) log('fixedUpdate'); }
  update() { if (this._frames++ < 2) log('update'); }
  lateUpdate() { if (this._frames <= 2) log('lateUpdate'); }
  onDisable() { log('onDisable'); }
  onDestroy() { log('onDestroy'); }
}

export class FieldProbe extends MonoBehaviour {
  speed = 2.5;
  @field('int') lives = 3;
  flag = true;
  label = 'hi';
  offset = new Vector2(1, 2);
  tint = new Color(1, 0, 0, 1);
  @field({ options: ['Idle', 'Run', 'Jump'] }) mode = 1;
  @field({ range: [0, 10], tooltip: 'a slider' }) ranged = 5;
  @field(GameObject) target: GameObject | null = null;
  @field(Prefab) prefab: Prefab | null = null;
  private _hidden = 42;

  start() {
    log(`fields:${this.speed}|${this.lives}|${this.flag}|${this.label}|${this.offset.x},${this.offset.y}|${this.tint.r}|${this.mode}|${this.ranged}|${this.target ? this.target.name : 'null'}|${this.prefab ? this.prefab.guid : 'null'}|${this._hidden}`);
  }
}

export class Collision3DProbe extends MonoBehaviour {
  onCollisionEnter(collision: { gameObject: GameObject }) { log(`collisionEnter3D:${this.name}<-${collision.gameObject.name}`); }
}

export class CollisionProbe extends MonoBehaviour {
  onCollisionEnter2D(collision: Collision2D) { log(`collisionEnter2D:${this.name}<-${collision.gameObject.name}`); }
  onCollisionExit2D(collision: Collision2D) { log(`collisionExit2D:${this.name}<-${collision.gameObject.name}`); }
  onTriggerEnter2D(other: Collider2D) { log(`triggerEnter2D:${this.name}<-${other.gameObject.name}`); }
}

/** Reads/writes a C++ script's DUALITY_PROPERTY fields and another TS script through getComponent. */
export class CrossProbe extends MonoBehaviour {
  start() {
    const native = this.getComponent<NativeScript>('NativeCounter');
    log(`native:${native ? native.get('Count') : 'missing'}`);
    if (native) native.set('Count', 41);
    const ts = this.getComponent(FieldProbe);
    log(`ts:${ts ? ts.speed : 'missing'}`);
    const rb = this.getComponent(Rigidbody2D);
    log(`rb:${rb ? 'yes' : 'no'}`);
  }
  ping() { log('ping'); }
}

export class SpawnProbe extends MonoBehaviour {
  @field(Prefab) prefab: Prefab | null = null;
  start() {
    const go = this.prefab ? instantiate(this.prefab, null, new Vector3(0, -3, 0)) : null;
    log(`spawned:${go ? go.name : 'null'}`);
    if (go) {
      const rb = go.getComponent(Rigidbody2D);
      log(`spawnedBody:${rb ? rb.mass > 0 : 'none'}`);
      log(`spawnedY:${Math.round(go.transform.position.y)}`);
      destroy(go, 0.05);
    }
  }
}

export class SpawnedProbe extends MonoBehaviour {
  awake() { log('spawnedAwake'); }
  onDestroy() { log('spawnedDestroyed'); }
}

export class CoroutineProbe extends MonoBehaviour {
  start() {
    this.startCoroutine(this.run());
  }
  *run() {
    log('co:begin');
    yield null;
    log('co:frame');
    yield new WaitForSeconds(0.05);
    log(`co:waited:${Time.time >= 0.05}`);
  }
}

export class ErrorProbe extends MonoBehaviour {
  update() {
    const missing: any = null;
    missing.explode(); // TypeError at a known TS line
  }
}

export class Mover extends MonoBehaviour {
  speed = 1;
  update() { this.transform.translate(this.speed * Time.deltaTime, 0, 0); }
}

/** Reads/writes a C++-defined ScriptableObject asset through an asset field. */
export class SoProbe extends MonoBehaviour {
  @field(ScriptableObject) data: ScriptableObject | null = null;
  start() {
    const so = this.data;
    log(`so:${so ? so.className : 'null'}|${so ? so.get('Health') : ''}|${so ? so.fieldNames.join(',') : ''}`);
    if (so) so.set('Coins', 99);
  }
}
