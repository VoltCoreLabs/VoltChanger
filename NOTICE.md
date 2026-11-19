# Attribution

VoltChanger is maintained by [VoltCoreLabs](https://github.com/VoltCoreLabs).
The launcher and the modified native module are distributed under
[GPL-3.0-only](LICENSE). Original copyright notices remain in the source files.

| Component | Origin | License / notice |
|---|---|---|
| Native skin changer base | [pastow/ovr-skinchanger](https://github.com/pastow/ovr-skinchanger), revision `3f131e9e6e75bc8a04e48ee3a5f81ef2f75c811b` | GPL-3.0; [upstream/LICENSE](upstream/LICENSE) |
| GPUI Kit | [gpui-kit](https://crates.io/crates/gpui-kit), pinned in Cargo.lock | Apache-2.0; dependency source notices apply |
| Golos Text | [Golos Text Project Authors / Google Fonts](https://github.com/googlefonts/golos-text) | SIL Open Font License 1.1; [font license](resources/fonts/OFL-GolosText.txt) |
| MinHook | Tsuda Kageyu and contributors | [Bundled BSD license](upstream/src/Resource/Minhook/LICENSE.txt); HDE notices retained in source |
| Protobuf | [protocolbuffers/protobuf](https://github.com/protocolbuffers/protobuf), v21.12 | BSD-3-Clause; fetched and hash-verified by CMake |
| Protocol schema snapshot | [SteamDatabase/Protobufs](https://github.com/SteamDatabase/Protobufs) | Revision in `vendor/protos/revision.txt`; fetched by `scripts/fetch-protos.ps1` |
| OpenDota identifiers | [odota/dotaconstants](https://github.com/odota/dotaconstants) | [MIT notice](resources/assistant/ODOTA-LICENSE.txt) |
| DotaCoach guides | [dotacoach.gg](https://dotacoach.gg/en/heroes) | Retrieved from public pages at runtime; factual test captures are attributed in `tests/fixtures/README.md` |

Dota 2 and Steam belong to Valve. VoltChanger is an independent community project
and is not affiliated with Valve or the data providers. Game models, textures,
audio, personal inventories and Steam credentials are not included.
