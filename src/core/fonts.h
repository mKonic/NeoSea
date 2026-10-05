#pragma once
// The typefaces NEO bundles, and where neosea finds its resources.
//
// On Linux NEO offers bundled faces in place of the Mac and Windows ones
// (Gelasio for Georgia, TeX Gyre Pagella for Palatino, …); a library written
// on a Mac still resolves its old names to those faces.

#include <QString>
#include <QStringList>

namespace neosea {

// $NEOSEA_RESOURCES, then <exe>/../share/neosea, then the source tree
QString resourcesDir();
// Registers every bundled face once; returns how many loaded
int registerBundledFonts();

// the Format menu's choices, in order
QStringList bodyFontChoices();
// the family a stored choice sets the page in: a bundled face, a legacy name
// mapped to one, or a font from the writer's own computer as it stands
QString bodyFontFamily(const QString &choice);
// "literary" / "fantasy" / "scifi"; empty for "none"
QString dropCapFamily(const QString &style);
inline const QString kScriptFamily = QStringLiteral("Courier Prime");

} // namespace neosea
