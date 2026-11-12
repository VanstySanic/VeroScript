# Installer dependency notices

`THIRD-PARTY-NOTICES.txt` retains complete upstream notices for the installer
application and tooling. It is embedded by the Core project and printed by
`--third-party-licenses`. The project MIT text is printed separately by
`--license`. Components present vary by platform and publish configuration;
the collection covers the desktop dependency graph rather than assigning MIT
to every component.

## Sources

| Component | Reviewed version | Notice source |
| --- | --- | --- |
| Avalonia desktop, rendering, theme and font integration packages | 11.3.18 | [Upstream license at the NuGet source commit](https://github.com/AvaloniaUI/Avalonia/blob/aa4265a99041adead000bda1cd6a51857e3b1bf1/licence.md) |
| MicroCom.Runtime | 0.11.0 | [Upstream license](https://github.com/kekekeks/MicroCom/blob/4b8a38f773c109bad558ee3713d9f16d80776e42/LICENSE), retaining the package's Nikita Tsukanov copyright |
| Tmds.DBus.Protocol | 0.21.3 | [COPYING at the NuGet source commit](https://github.com/tmds/Tmds.DBus/blob/8cb04f66c330b64244e996ff68f685f27f7381a0/COPYING) |
| Inter fonts bundled by Avalonia.Fonts.Inter | 3.019 | [Inter 3.19 license](https://github.com/rsms/inter/blob/v3.19/LICENSE.txt); the bundled font metadata identifies the 2020 Inter Project Authors copyright and OFL 1.1 |
| Avalonia.Angle.Windows.Natives | 2.1.25547.20250602 | `LICENSE` from its restored NuGet package |
| SkiaSharp and desktop native asset packages | 2.88.9 | `LICENSE.txt` and `THIRD-PARTY-NOTICES.txt` from their restored NuGet packages |
| HarfBuzzSharp and desktop native asset packages | 8.3.1.1 | `LICENSE.txt` and `THIRD-PARTY-NOTICES.txt` from their restored NuGet packages |
| System.IO.Pipelines | 8.0.0 | `LICENSE.TXT` and `THIRD-PARTY-NOTICES.TXT` from its restored NuGet package |
| Self-contained .NET runtime | 9.0.20 | `LICENSE.TXT` and `THIRD-PARTY-NOTICES.TXT` from the restored runtime pack |

Identical notice bodies shared by multiple packages appear once. Original
copyright, permission, conditions and disclaimer text is retained.

## Dependency updates

When updating packages or the self-contained runtime, review the restored
`project.assets.json` dependency graph and the notices in those exact package
versions. Update this source table and the full embedded collection together,
including notices for newly introduced dependencies and native assets. Font
licenses follow the bundled font version, independently of the integration
package's own license.

Publisher scripts compare both notice commands with their source files and
reject missing text, changed text or failed commands. Rebuild installers after
notice changes; previously published executables keep their existing resources.
