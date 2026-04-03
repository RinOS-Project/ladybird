/*
 * Copyright (c) 2025, Sam Atkins <sam@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibGfx/Font/FontSupport.h>

#if !defined(AK_OS_RINOS)
#include <harfbuzz/hb.h>
#endif

namespace Gfx {

bool font_format_is_supported(FontFormat const format)
{
    // FIXME: Determine these automatically.
    switch (format) {
    case FontFormat::EmbeddedOpenType:
        return false;
    case FontFormat::OpenType:
        return true;
    case FontFormat::SVG:
        return false;
    case FontFormat::TrueType:
        return true;
    case FontFormat::TrueTypeCollection:
        return true;
    case FontFormat::WOFF:
        return true;
    case FontFormat::WOFF2:
        return true;
    }

    return false;
}

bool font_tech_is_supported(FontTech const font_tech)
{
    // https://drafts.csswg.org/css-fonts-4/#font-tech-definitions
    // FIXME: Determine these automatically.
    switch (font_tech) {
    case FontTech::FeaturesOpentype:
#if defined(AK_OS_RINOS)
        return false;
#else
        return true;
#endif
    case FontTech::FeaturesAat:
#if defined(AK_OS_RINOS)
        return false;
#else
        return true;
#endif
    case FontTech::FeaturesGraphite:
        // Silf, Glat , Gloc , Feat and Sill. HarfBuzz may or may not be built with support for it.
#if defined(AK_OS_RINOS)
        return false;
#else
#if HB_HAS_GRAPHITE
        return true;
#else
        return false;
#endif
#endif
    case FontTech::Variations:
        // avar, cvar, fvar, gvar, HVAR, MVAR, STAT, and VVAR, supported by HarfBuzz
        // FIXME: This does not actually seem to work and causes issues with the font weight on https://ladybird.org
        return false;
    case FontTech::ColorColrv0:
    case FontTech::ColorColrv1:
        return false;
    case FontTech::ColorSvg:
        return false;
    case FontTech::ColorSbix:
        return false;
    case FontTech::ColorCbdt:
        return false;
    case FontTech::Palettes:
        return false;
    case FontTech::Incremental:
        // Incremental Font Transfer: https://w3c.github.io/IFT/Overview.html
        return false;
    // https://drafts.csswg.org/css-fonts-5/#font-tech-definitions
    case FontTech::Avar2:
        return false;
    }
    return false;
}

}
