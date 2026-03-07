/**
 * dragDropTypes.ts — Shared types for the drag-and-drop system.
 *
 * Defines the asset payload carried during drag operations and the
 * MIME types used for DataTransfer serialization.
 */

/** Asset types recognised by the drag-and-drop system. */
export type AssetType = 'Mesh' | 'Material' | 'Texture' | 'AudioClip' | 'Script';

/** Payload carried during an asset drag operation. */
export interface AssetDragPayload {
    /** Engine resource path (e.g. "res://meshes/house.glb"). */
    readonly resPath: string;
    /** Asset type — determines which component to attach on drop. */
    readonly type: AssetType;
    /** Human-readable name shown during drag (e.g. "house.glb"). */
    readonly displayName: string;
    /** Optional thumbnail URI for drag preview. */
    readonly thumbnailUri?: string;
}

/** MIME type for scene tree node drag data. */
export const MIME_SCENE_TREE = 'application/vnd.uzengine.scenetree';

/** MIME type for asset drag data. */
export const MIME_ASSET = 'application/vnd.uzengine.asset';
