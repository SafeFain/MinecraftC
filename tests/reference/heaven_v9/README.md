# Heaven v9 immutable reference

Source bytes frozen from 988fbd4 before P1 migration. These files are test data,
never runtime providers. The manifest records the original SHA-256 hashes.
Do not regenerate these snapshots from the migrated implementation.

`FrozenHeavenV9`, `FrozenSkywayPolicy`, `FrozenHeavenLod`, `FrozenLoot`,
`FrozenEnvironment` and `FrozenParticleSystem` are independently compiled or
included adaptations of those snapshots. They do not invoke the migrated
provider. Namespace/include substitutions keep them separate from production;
the particle fixture substitutes frozen generation only for biome reads.
Raw snapshot hashes are verified by the independent content CTest.
