import { Color, Input, KeyCode, MonoBehaviour, SpriteRenderer, Time, Vector3, field } from 'duality';

/** Moves left/right (keyboard A/D or arrows, 3DS Circle Pad), bobs and spins.
 *  Without input it patrols on its own so the demo is alive in a headless run. */
export class Mover extends MonoBehaviour {
  @field({ range: [0, 5], tooltip: 'world units per second' }) speed = 1.5;
  @field({ range: [0, 1] }) bobAmplitude = 0.15;
  bobFrequency = 2;
  spinDegreesPerSecond = 90;
  tint = new Color(1, 0.6, 0.2, 1);
  limitX = 1.6;

  private _home = new Vector3();
  private _direction = 1;
  private _sprite: SpriteRenderer | null = null;

  start() {
    this._home = this.transform.position;
    this._sprite = this.getComponent(SpriteRenderer);
    if (this._sprite) this._sprite.color = this.tint;
    this.print(`Mover started at ${this._home}, speed ${this.speed}`);
  }

  update() {
    const dt = Time.deltaTime;
    let input = Input.getAxis('Horizontal');
    if (input === 0) {
      // Auto-patrol between -limitX and +limitX.
      input = this._direction;
    }
    const p = this.transform.position;
    let x = p.x + input * this.speed * dt;
    if (x > this.limitX) { x = this.limitX; this._direction = -1; }
    if (x < -this.limitX) { x = -this.limitX; this._direction = 1; }
    const y = this._home.y + Math.sin(Time.time * this.bobFrequency * Math.PI) * this.bobAmplitude;
    this.transform.setPosition(x, y, p.z);
    this.transform.rotate(0, 0, this.spinDegreesPerSecond * dt);

    if (Input.getKeyDown(KeyCode.Space) || Input.getKeyDown(KeyCode.GamepadA)) {
      this.spinDegreesPerSecond = -this.spinDegreesPerSecond;
      this.print('Mover: spin reversed');
    }
  }
}
