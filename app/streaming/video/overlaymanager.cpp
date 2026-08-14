#include "overlaymanager.h"
#include "path.h"

using namespace Overlay;

OverlayManager::OverlayManager() :
    m_Renderer(nullptr),
    m_FontData(Path::readDataFile("ModeSeven.ttf"))
{
    memset(m_Overlays, 0, sizeof(m_Overlays));

    m_Overlays[OverlayType::OverlayDebug].color = {0xD0, 0xD0, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayDebug].fontSize = 20;

    m_Overlays[OverlayType::OverlayStatusUpdate].color = {0xCC, 0x00, 0x00, 0xFF};
    m_Overlays[OverlayType::OverlayStatusUpdate].fontSize = 36;

    m_Overlays[OverlayType::OverlayQuickMenu].color = {0xFF, 0xFF, 0xFF, 0xFF};
    m_Overlays[OverlayType::OverlayQuickMenu].fontSize = 30;

    // While TTF will usually not be initialized here, it is valid for that not to
    // be the case, since Session destruction is deferred and could overlap with
    // the lifetime of a new Session object.
    //SDL_assert(TTF_WasInit() == 0);

    if (TTF_Init() != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "TTF_Init() failed: %s",
                    TTF_GetError());
        return;
    }
}

OverlayManager::~OverlayManager()
{
    for (int i = 0; i < OverlayType::OverlayMax; i++) {
        if (m_Overlays[i].surface != nullptr) {
            SDL_FreeSurface(m_Overlays[i].surface);
        }
        if (m_Overlays[i].font != nullptr) {
            TTF_CloseFont(m_Overlays[i].font);
        }
    }

    TTF_Quit();

    // For similar reasons to the comment in the constructor, this will usually,
    // but not always, deinitialize TTF. In the cases where Session objects overlap
    // in lifetime, there may be an additional reference on TTF for the new Session
    // that means it will not be cleaned up here.
    //SDL_assert(TTF_WasInit() == 0);
}

bool OverlayManager::isOverlayEnabled(OverlayType type)
{
    return m_Overlays[type].enabled;
}

char* OverlayManager::getOverlayText(OverlayType type)
{
    return m_Overlays[type].text;
}

void OverlayManager::updateOverlayText(OverlayType type, const char* text)
{
    SDL_utf8strlcpy(m_Overlays[type].text, text, sizeof(m_Overlays[0].text));
    setOverlayTextUpdated(type);
}

int OverlayManager::getOverlayMaxTextLength()
{
    return sizeof(m_Overlays[0].text);
}

int OverlayManager::getOverlayFontSize(OverlayType type)
{
    return m_Overlays[type].fontSize;
}

SDL_Surface* OverlayManager::getUpdatedOverlaySurface(OverlayType type)
{
    // If a new surface is available, return it. If not, return nullptr.
    // Caller must free the surface on success.
    return (SDL_Surface*)SDL_AtomicSetPtr((void**)&m_Overlays[type].surface, nullptr);
}

void OverlayManager::updateQuickMenuSurface(int width, int height, const QStringList& labels,
                                            int selectedIndex, bool editMode, bool monitorSubmenu)
{
    if (width <= 0 || height <= 0 || m_FontData.isEmpty()) {
        return;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (surface == nullptr) {
        return;
    }
    SDL_SetSurfaceBlendMode(surface, SDL_BLENDMODE_BLEND);
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 8, 10, 16, 205));

    const int scale = qMax(1, qMin(width, height) / 720);
    const int margin = 42 * scale;
    const int gap = 18 * scale;
    const int headerHeight = 112 * scale;
    const int columns = width >= 900 ? 3 : 2;
    const int rows = qMax(1, (labels.size() + columns - 1) / columns);
    const int cardWidth = (width - margin * 2 - gap * (columns - 1)) / columns;
    const int cardHeight = qMin(190 * scale,
                                (height - headerHeight - margin * 2 - gap * (rows - 1)) / rows);

    TTF_Font* titleFont = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                         1, 34 * scale);
    TTF_Font* cardFont = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                        1, 24 * scale);
    TTF_Font* hintFont = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                        1, 16 * scale);
    auto drawCentered = [surface](TTF_Font* font, const QString& text, SDL_Color color,
                                  const SDL_Rect& box) {
        if (font == nullptr) return;
        SDL_Surface* rendered = TTF_RenderUTF8_Blended_Wrapped(font, text.toUtf8().constData(),
                                                               color, qMax(1, box.w - 20));
        if (rendered == nullptr) return;
        SDL_Rect dst = {box.x + (box.w - rendered->w) / 2,
                        box.y + (box.h - rendered->h) / 2,
                        rendered->w, rendered->h};
        SDL_BlitSurface(rendered, nullptr, surface, &dst);
        SDL_FreeSurface(rendered);
    };

    SDL_Rect titleRect = {margin, margin / 2, width - margin * 2, 60 * scale};
    drawCentered(titleFont, monitorSubmenu ? QStringLiteral("MONITOR SWITCH") :
                 (editMode ? QStringLiteral("QUICK MENU  -  CARD HELD") : QStringLiteral("QUICK MENU")),
                 {255, 255, 255, 255}, titleRect);
    SDL_Rect hintRect = {margin, titleRect.y + titleRect.h, width - margin * 2, 40 * scale};
    drawCentered(hintFont,
                 monitorSubmenu ? QStringLiteral("Select monitor 1-4 with D-pad and Cross   Circle: back") :
                 (editMode ? QStringLiteral("D-pad: move held card   Square: drop and save   Circle: cancel")
                           : QStringLiteral("D-pad: navigate   Cross: select   Square: pick up card   Circle: back")),
                 {190, 194, 205, 255}, hintRect);

    const int gridY = margin + headerHeight;
    for (int i = 0; i < labels.size(); i++) {
        const int row = i / columns;
        const int col = i % columns;
        SDL_Rect card = {margin + col * (cardWidth + gap), gridY + row * (cardHeight + gap),
                         cardWidth, cardHeight};
        const bool selected = i == selectedIndex;
        SDL_FillRect(surface, &card, SDL_MapRGBA(surface->format,
                     selected ? 116 : 38, selected ? 72 : 42, selected ? 166 : 54, 242));
        if (selected) {
            const int border = 4 * scale;
            SDL_Rect top = {card.x, card.y, card.w, border};
            SDL_Rect bottom = {card.x, card.y + card.h - border, card.w, border};
            SDL_Rect left = {card.x, card.y, border, card.h};
            SDL_Rect right = {card.x + card.w - border, card.y, border, card.h};
            Uint32 accent = SDL_MapRGBA(surface->format, 218, 145, 255, 255);
            SDL_FillRect(surface, &top, accent); SDL_FillRect(surface, &bottom, accent);
            SDL_FillRect(surface, &left, accent); SDL_FillRect(surface, &right, accent);
        }
        drawCentered(cardFont, labels.at(i), {255, 255, 255, 255}, card);
    }

    if (titleFont) TTF_CloseFont(titleFont);
    if (cardFont) TTF_CloseFont(cardFont);
    if (hintFont) TTF_CloseFont(hintFont);

    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[OverlayType::OverlayQuickMenu].surface, surface);
    m_Overlays[OverlayType::OverlayQuickMenu].enabled = true;
    if (m_Renderer != nullptr) m_Renderer->notifyOverlayUpdated(OverlayType::OverlayQuickMenu);
    if (oldSurface != nullptr) SDL_FreeSurface(oldSurface);
}

void OverlayManager::setOverlayTextUpdated(OverlayType type)
{
    // Only update the overlay state if it's enabled. If it's not enabled,
    // the renderer has already been notified by setOverlayState().
    if (m_Overlays[type].enabled) {
        notifyOverlayUpdated(type);
    }
}

void OverlayManager::setOverlayState(OverlayType type, bool enabled)
{
    bool stateChanged = m_Overlays[type].enabled != enabled;

    m_Overlays[type].enabled = enabled;

    if (stateChanged) {
        if (!enabled) {
            // Set the text to empty string on disable
            m_Overlays[type].text[0] = 0;
        }

        notifyOverlayUpdated(type);
    }
}

SDL_Color OverlayManager::getOverlayColor(OverlayType type)
{
    return m_Overlays[type].color;
}

void OverlayManager::setOverlayRenderer(IOverlayRenderer* renderer)
{
    m_Renderer = renderer;
}

void OverlayManager::notifyOverlayUpdated(OverlayType type)
{
    if (m_Renderer == nullptr) {
        return;
    }

    // Construct the required font to render the overlay
    if (m_Overlays[type].font == nullptr) {
        if (m_FontData.isEmpty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SDL overlay font failed to load");
            return;
        }

        // m_FontData must stay around until the font is closed
        m_Overlays[type].font = TTF_OpenFontRW(SDL_RWFromConstMem(m_FontData.constData(), m_FontData.size()),
                                               1,
                                               m_Overlays[type].fontSize);
        if (m_Overlays[type].font == nullptr) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont() failed: %s",
                        TTF_GetError());

            // Can't proceed without a font
            return;
        }
    }

    // Exchange the old surface with the new one
    SDL_Surface* oldSurface = (SDL_Surface*)SDL_AtomicSetPtr(
        (void**)&m_Overlays[type].surface,
        m_Overlays[type].enabled ?
            // The _Wrapped variant is required for line breaks to work
            RenderTextOutlinedWrapped(m_Overlays[type].font,
                                      m_Overlays[type].text,
                                      m_Overlays[type].color,
                                      {0, 0, 0, 255},
                                      4,
                                      1024)
            : nullptr);

    // Notify the renderer
    m_Renderer->notifyOverlayUpdated(type);

    // Free the old surface
    if (oldSurface != nullptr) {
        SDL_FreeSurface(oldSurface);
    }
}

SDL_Surface* OverlayManager::RenderTextOutlinedWrapped(TTF_Font* font, const char* text, SDL_Color textColor, SDL_Color outlineColor, int outlineWidth, int wrapWidth) {
    if (text == nullptr || text[0] == '\0') {
        return nullptr;
    }

    int oldOutline = TTF_GetFontOutline(font);
    TTF_SetFontOutline(font, outlineWidth);

    // Verify that the string won't require wrapping (which could cause the outline and the text
    // to diverge due to different wrapping positions).
    //
    // FIXME: We do this rather than just disabling wrapping entirely (wrapWidth = 0) because we
    // need further testing to ensure that all renderers can handle non-NPOT overlay textures.
    for (const QString& line : QString(text).split('\n')) {
        int extent, count;
        if (TTF_MeasureUTF8(font, line.toUtf8(), wrapWidth, &extent, &count) == 0 && count < line.size()) {
            // If it requires wrapping, render it without the outline
            TTF_SetFontOutline(font, oldOutline);
            return TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
        }
    }

    // Draw text twice, but outline is a bit bigger
    auto outlineSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, outlineColor, wrapWidth);
    TTF_SetFontOutline(font, 0);
    auto textSurface = TTF_RenderUTF8_Blended_Wrapped(font, text, textColor, wrapWidth);
    TTF_SetFontOutline(font, oldOutline);

    if (outlineSurface == nullptr || textSurface == nullptr) {
        SDL_FreeSurface(outlineSurface);
        SDL_FreeSurface(textSurface);
        return nullptr;
    }

    // Merge the texts
    SDL_Rect dst = { outlineWidth, outlineWidth, textSurface->w, textSurface->h };
    SDL_BlitSurface(textSurface, nullptr, outlineSurface, &dst);

    SDL_FreeSurface(textSurface);
    return outlineSurface;
}


