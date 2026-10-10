## 1. Localise
- [ ] 1.1 A diagnostic layout shift (an allocation before the scene's resources) reproduces the one-code-value moves on the three references.
- [ ] 1.2 Region-dump records per pass with and without the shift: the first record that differs names the pass.

## 2. Fix
- [ ] 2.1 Initialise or clear the resource before its first read, or remove the index dependence.

## 3. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 3.1 Update or create documentation covering the implementation
- [ ] 3.2 Write tests covering the new behavior
- [ ] 3.3 Run tests and confirm they pass
