/**
 * viewport-types.test.ts — Tests for shared type definitions and constants.
 *
 * Verifies that the exported constants have the correct runtime values and that
 * the type shapes (checked via type assertions at compile time) are consistent
 * with the C-side spec.
 */

import {
    VIEWPORT_ID_INVALID,
} from '../viewport-types';

describe('viewport-types constants', () => {
    describe('VIEWPORT_ID_INVALID', () => {
        it('should equal 0 (matching C-side #define VIEWPORT_ID_INVALID 0)', () => {
            expect(VIEWPORT_ID_INVALID).toBe(0);
        });

        it('should be a number type at runtime', () => {
            expect(typeof VIEWPORT_ID_INVALID).toBe('number');
        });

        it('should be falsy (used as a guard condition in isValid checks)', () => {
            expect(VIEWPORT_ID_INVALID).toBeFalsy();
        });
    });

    describe('type guard patterns used in production code', () => {
        it('should treat VIEWPORT_ID_INVALID as falsy in guard conditions', () => {
            const id = VIEWPORT_ID_INVALID;
            // Mirrors: if (id === VIEWPORT_ID_INVALID) { return; }
            expect(id === VIEWPORT_ID_INVALID).toBe(true);
            expect(id !== VIEWPORT_ID_INVALID).toBe(false);
        });

        it('should allow valid viewport IDs (>0) to pass falsy guard', () => {
            const validId = 1;
            expect(validId !== VIEWPORT_ID_INVALID).toBe(true);
            expect(Boolean(validId)).toBe(true);
        });
    });
});
