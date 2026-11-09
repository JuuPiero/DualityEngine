import { Collider2D, Collision, Collision2D, Color, Debug, MonoBehaviour, SpriteRenderer, Time, field } from 'duality';

/** Logs collision/trigger events (2D and 3D) to the Console and flashes the sprite on hits. */
export class CollisionLogger extends MonoBehaviour {
  /** Log every Nth event only (the spawner can produce many). */
  @field('int') logEvery = 1;
  flashColor = new Color(1, 1, 1, 1);

  private _count = 0;
  private _baseColor: Color | null = null;
  private _flashUntil = 0;
  private _sprite: SpriteRenderer | null = null;

  awake() {
    this._sprite = this.getComponent(SpriteRenderer);
    this._baseColor = this._sprite ? this._sprite.color : null;
  }

  onCollisionEnter2D(collision: Collision2D) { this.record('collision', collision.gameObject.name, true); }
  onTriggerEnter2D(other: Collider2D) { this.record('trigger', other.gameObject.name, true); }
  onTriggerExit2D(other: Collider2D) { this.record('trigger exit', other.gameObject.name, false); }
  onCollisionEnter(collision: Collision) { this.record('3D collision', collision.gameObject.name, true); }

  update() {
    if (this._sprite && this._baseColor && this._flashUntil > 0 && Time.time > this._flashUntil) {
      this._sprite.color = this._baseColor;
      this._flashUntil = 0;
    }
  }

  private record(kind: string, other: string, flash: boolean) {
    this._count++;
    if (this._count % Math.max(1, this.logEvery) === 0)
      Debug.log(`[CollisionLogger] ${this.name}: ${kind} with ${other} (#${this._count}, t=${Time.time.toFixed(2)}s)`);
    if (flash && this._sprite) {
      this._sprite.color = this.flashColor;
      this._flashUntil = Time.time + 0.1;
    }
  }
}
