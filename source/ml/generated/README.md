Place the DEEPCRAFT Studio **C source, Queue API without timestamps** export
(`model.c`, `model.h`, and any export dependencies) here. Use the `IMAI_` prefix
and include the model's preprocessing/sliding window in the export.

No generated model is included yet. The repository has a `.h5` training
artifact under `DeepCraft/IMUexample`, not an MCU-deployable C library.

Check `../activity_model_config.h` against the actual export, replay the supplied
test vectors, then build with `ACTIVITY_MODEL_ENABLED=1` and
`ACTIVITY_MODEL_CONTRACT_VERIFIED=1`. See `documentation/wearable.md` for the
full deployment and enclosure workflow. Unsupported APIs fail compilation.
