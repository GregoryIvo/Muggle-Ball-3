<!-- What does this change, and why? -->

## Checklist
- [ ] Builds locally (`cmake --build build`) and tests pass (`ctest --test-dir build` and `python3 -m unittest discover -s tests -p 'test_*.py'`)
- [ ] Added or updated tests for the behaviour this changes
- [ ] Updated the docs if this changes how something works
- [ ] No game assets or data committed (art, audio, levels, `.dat` / `.mb3` / `.x`, the original exe)
- [ ] Regenerated shaders with `tools/build_shaders.py` if I edited `engine/shaders/`
