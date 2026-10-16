/** Holds dependent frames after a gap until an IDR restores the reference chain. */
export class FrameGate {
  private awaitingKey = true;

  get awaitingKeyframe(): boolean {
    return this.awaitingKey;
  }

  shouldDeliver(isKeyframe: boolean): boolean {
    if (this.awaitingKey && !isKeyframe) return false;
    this.awaitingKey = false;
    return true;
  }

  onDropped(): void {
    this.awaitingKey = true;
  }

  arm(): void {
    this.awaitingKey = true;
  }
}
