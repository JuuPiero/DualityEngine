import { Application, Debug, GameObject, MonoBehaviour, Prefab, Random, Vector3, WaitForSeconds, field } from 'duality';

/** Field-driven spawner: instantiates `prefab` every `interval` seconds at a random x, keeping at
 *  most `maxAlive` copies (each copy's FallingBox script destroys itself after its lifetime). */
export class Spawner extends MonoBehaviour {
  @field(Prefab) prefab: Prefab | null = null;
  @field({ range: [0.05, 5], header: 'Timing' }) interval = 0.6;
  @field({ type: 'int', range: [1, 50] }) maxAlive = 8;
  @field({ range: [0, 2] }) spreadX = 1.4;
  @field({ options: ['Random', 'Left to right'] }) pattern = 0;

  private _spawned: GameObject[] = [];
  private _total = 0;
  private _nextX = -1;

  start() {
    if (!this.prefab) {
      Debug.warn('Spawner: no prefab assigned -- drag a .prefab onto the field');
      return;
    }
    this.startCoroutine(this.spawnLoop());
    this.invokeRepeating(() => this.report(), 2, 2);
  }

  private *spawnLoop() {
    for (;;) {
      this._spawned = this._spawned.filter((go) => go.isValid);
      if (this._spawned.length < this.maxAlive) this.spawnOne();
      yield new WaitForSeconds(this.interval);
    }
  }

  private spawnOne() {
    const origin = this.transform.position;
    let x: number;
    if (this.pattern === 0) x = Random.range(-this.spreadX, this.spreadX);
    else { x = this._nextX * this.spreadX; this._nextX = this._nextX >= 1 ? -1 : this._nextX + 0.25; }
    const go = this.instantiate(this.prefab!, null, new Vector3(origin.x + x, origin.y, origin.z));
    if (!go) return;
    go.name = `Box${++this._total}`;
    this._spawned.push(go);
  }

  private report() {
    const alive = this._spawned.filter((go) => go.isValid).length;
    Debug.log(`[Spawner] spawned ${this._total} total, ${alive} alive, script heap ${(Application.scriptMemory / 1024).toFixed(0)} KB (${Application.platform})`);
  }
}
