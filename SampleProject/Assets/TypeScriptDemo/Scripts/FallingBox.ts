import { Color, MonoBehaviour, Random, Rigidbody2D, SpriteRenderer, Vector2, field } from 'duality';

/** Lives inside the FallingBox prefab: random tint, a small sideways kick, self-destruct. */
export class FallingBox extends MonoBehaviour {
  @field({ range: [0.5, 10] }) lifetime = 4;
  kick = 0.6;

  awake() {
    const sprite = this.getComponent(SpriteRenderer);
    if (sprite) sprite.color = new Color(Random.range(0.3, 1), Random.range(0.3, 1), Random.range(0.3, 1), 1);
  }

  start() {
    const body = this.getComponent(Rigidbody2D);
    if (body) {
      // Sideways kick as a velocity change (independent of the box's tiny mass).
      body.velocity = new Vector2(Random.range(-this.kick, this.kick), 0);
      body.angularVelocity = Random.range(-180, 180);
    }
    this.destroy(this.gameObject, this.lifetime);
  }
}
