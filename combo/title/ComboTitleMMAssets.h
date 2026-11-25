#ifndef COMBO_TITLE_MM_ASSETS_H
#define COMBO_TITLE_MM_ASSETS_H

// MM title logo assets (ROM-extracted, in mm.o2r). Redeclared here: soh cannot include mm headers. Drawn inside a
// gSPComboRMPush(.., "mm") bracket by the combined title screen (ComboTitleLogos.h) and the file select's
// Majora's Mask quest (z_file_choose.c).
#define dgTitleScreenMajorasMaskTex "__OTR__objects/object_mag/gTitleScreenMajorasMaskTex"
static const ALIGN_ASSET(2) char gTitleScreenMajorasMaskTex[] = dgTitleScreenMajorasMaskTex;
#define dgTitleScreenZeldaLogoTex "__OTR__objects/object_mag/gTitleScreenZeldaLogoTex"
static const ALIGN_ASSET(2) char gTitleScreenZeldaLogoTex[] = dgTitleScreenZeldaLogoTex;
#define dgTitleScreenTheLegendOfTextTex "__OTR__objects/object_mag/gTitleScreenTheLegendOfTextTex"
static const ALIGN_ASSET(2) char gTitleScreenTheLegendOfTextTex[] = dgTitleScreenTheLegendOfTextTex;
#define dgTitleScreenMajorasMaskSubtitleTex "__OTR__objects/object_mag/gTitleScreenMajorasMaskSubtitleTex"
static const ALIGN_ASSET(2) char gTitleScreenMajorasMaskSubtitleTex[] = dgTitleScreenMajorasMaskSubtitleTex;
#define dgTitleScreenMajorasMaskSubtitleMaskTex "__OTR__objects/object_mag/gTitleScreenMajorasMaskSubtitleMaskTex"
static const ALIGN_ASSET(2) char gTitleScreenMajorasMaskSubtitleMaskTex[] = dgTitleScreenMajorasMaskSubtitleMaskTex;

#endif /* COMBO_TITLE_MM_ASSETS_H */
