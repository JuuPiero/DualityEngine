import { CharacterController, ControllerColliderHit, Debug, HingeJoint2D, MonoBehaviour, Time, Vector3, field } from 'duality';

/** ComponentsDemo: walks a Character Controller back and forth (SimpleMove applies gravity). */
export class DemoWalker extends MonoBehaviour {
  @field({ range: [0.1, 4] }) speed = 1.2;
  @field() halfRange = 1.4;

  private _controller: CharacterController | null = null;
  private _direction = 1;
  private _startX = 0;
  private _hits = 0;

  awake() {
    this._controller = this.getComponent(CharacterController);
    this._startX = this.transform.position.x;
  }

  update() {
    if (!this._controller) return;
    const x = this.transform.position.x;
    if (x > this._startX + this.halfRange) this._direction = -1;
    else if (x < this._startX - this.halfRange) this._direction = 1;
    this._controller.simpleMove(new Vector3(this.speed * this._direction, 0, 0));
  }

  onControllerColliderHit(hit: ControllerColliderHit) {
    // Turn around at walls (anything hit sideways).
    if (Math.abs(hit.normal.x) > 0.7) this._direction = hit.normal.x > 0 ? 1 : -1;
    if (++this._hits === 1) Debug.log('DemoWalker: first controller hit', hit.gameObject ? hit.gameObject.name : '?');
  }
}

/** ComponentsDemo: drives the chain's top hinge motor back and forth (live joint edits). */
export class DemoMotorSwing extends MonoBehaviour {
  @field() period = 3;

  update() {
    const hinge = this.getComponent(HingeJoint2D);
    if (!hinge) return;
    const phase = Math.sin((Time.time / this.period) * Math.PI * 2);
    hinge.motor = { motorSpeed: phase * 120, maxMotorTorque: 2 };
  }

  onJointBreak2D() { Debug.log('DemoMotorSwing: joint broke'); }
}
