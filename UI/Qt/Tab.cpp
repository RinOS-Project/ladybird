/*
 * Copyright (c) 2022, Andreas Kling <andreas@ladybird.org>
 * Copyright (c) 2022, Matthew Costa <ucosty@gmail.com>
 * Copyright (c) 2024, Jamie Mansfield <jmansfield@cadixdev.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibURL/URL.h>
#include <LibWeb/HTML/SelectedFile.h>
#include <LibWebView/Application.h>
#include <UI/Qt/BrowserWindow.h>
#include <UI/Qt/Icon.h>
#include <UI/Qt/Menu.h>
#include <UI/Qt/Settings.h>
#include <UI/Qt/StringUtils.h>

#include <QColorDialog>
#include <QByteArray>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QGeoCoordinate>
#include <QGeoPositionInfo>
#include <QGeoPositionInfoSource>
#include <QImage>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPointer>
#include <QResizeEvent>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <limits>
#include <memory>

namespace Ladybird {

class HamburgerButton final : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    virtual void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            show_menu();
        else
            QToolButton::mousePressEvent(event);
    }

    virtual void keyPressEvent(QKeyEvent* event) override
    {
        if (first_is_one_of(event->key(), Qt::Key_Select, Qt::Key_Space))
            show_menu();
        else
            QToolButton::keyPressEvent(event);
    }

private:
    void show_menu()
    {
        auto* menu = this->menu();
        VERIFY(menu);

        auto bottom_right = mapToGlobal(rect().bottomRight());
        auto menu_width = menu->sizeHint().width();

        menu->popup(QPoint { bottom_right.x() - menu_width, bottom_right.y() });
    }
};

static QIcon default_favicon()
{
    static QIcon icon = load_icon_from_uri("resource://icons/48x48/app-browser.png"sv);
    return icon;
}

static Optional<QString> canonical_geolocation_origin(ByteString const& origin)
{
    auto encoded_origin = QByteArray(origin.characters(), static_cast<qsizetype>(origin.length()));
    auto url = QUrl::fromEncoded(encoded_origin, QUrl::ParsingMode::StrictMode);
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
        !url.path().isEmpty() || !url.query().isEmpty() || !url.fragment().isEmpty())
        return {};

    auto scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))
        return {};
    auto port = url.port(-1);
    if ((scheme == QStringLiteral("http") && port == 80) ||
        (scheme == QStringLiteral("https") && port == 443))
        port = -1;

    auto host = url.host().toLower();
    if (host.contains(QLatin1Char(':')) && !host.startsWith(QLatin1Char('[')))
        host = QLatin1Char('[') + host + QLatin1Char(']');
    auto canonical = scheme + QStringLiteral("://") + host;
    if (port != -1)
        canonical += QStringLiteral(":") + QString::number(port);
    if (canonical.toUtf8() != encoded_origin)
        return {};
    return canonical;
}

Tab::Tab(BrowserWindow* window, RefPtr<WebView::WebContentClient> parent_client, size_t page_index)
    : QWidget(window)
    , m_window(window)
{
    m_layout = new QBoxLayout(QBoxLayout::Direction::TopToBottom, this);
    m_layout->setSpacing(0);
    m_layout->setContentsMargins(0, 0, 0, 0);

    auto view_initial_state = WebContentViewInitialState {
        .maximum_frames_per_second = window->refresh_rate(),
    };

    m_view = new WebContentView(this, parent_client, page_index, AK::move(view_initial_state));
    m_find_in_page = new FindInPageWidget(this, m_view);
    m_find_in_page->setVisible(false);
    m_toolbar = new QToolBar(this);
    m_location_edit = new LocationEdit(this);
    m_bookmarks_bar = new BookmarksBar(this);

    m_hover_label = new HyperlinkLabel(this);
    m_hover_label->hide();
    m_hover_label->setFrameShape(QFrame::Shape::Box);
    m_hover_label->setAutoFillBackground(true);

    QObject::connect(m_hover_label, &HyperlinkLabel::mouse_entered, [this] {
        update_hover_label();
    });

    auto* focus_location_editor_action = new QAction("Edit Location", this);
    focus_location_editor_action->setShortcuts({ QKeySequence("Ctrl+L"), QKeySequence("Alt+D") });
    addAction(focus_location_editor_action);

    m_layout->addWidget(m_toolbar);
    m_layout->addWidget(m_bookmarks_bar);
    m_layout->addWidget(m_view);
    m_layout->addWidget(m_find_in_page);

    m_hamburger_button = new HamburgerButton(m_toolbar);
    m_hamburger_button->setText("Show &Menu");
    m_hamburger_button->setToolTip("Show Menu");
    m_hamburger_button->setIcon(create_tvg_icon_with_theme_colors("hamburger", palette()));
    m_hamburger_button->setPopupMode(QToolButton::InstantPopup);
    m_hamburger_button->setMenu(&m_window->hamburger_menu());
    m_hamburger_button->setStyleSheet(":menu-indicator {image: none}");

    QObject::connect(&m_window->hamburger_menu(), &QMenu::aboutToShow, m_hamburger_button, [this]() {
        m_hamburger_button->setDown(true);
    });
    QObject::connect(&m_window->hamburger_menu(), &QMenu::aboutToHide, m_hamburger_button, [this]() {
        m_hamburger_button->setDown(false);
    });

    m_navigate_back_action = create_application_action(*this, view().navigate_back_action());
    m_navigate_forward_action = create_application_action(*this, view().navigate_forward_action());
    m_reload_action = create_application_action(*this, WebView::Application::the().reload_action());

    recreate_toolbar_icons();

    m_favicon = default_favicon();

    m_page_context_menu = create_context_menu(*this, view(), view().page_context_menu());
    m_link_context_menu = create_context_menu(*this, view(), view().link_context_menu());
    m_image_context_menu = create_context_menu(*this, view(), view().image_context_menu());
    m_media_context_menu = create_context_menu(*this, view(), view().media_context_menu());

    m_toolbar->addAction(m_navigate_back_action);
    m_toolbar->addAction(m_navigate_forward_action);
    m_toolbar->addAction(m_reload_action);
    m_toolbar->addWidget(m_location_edit);
    m_toolbar->addAction(create_application_action(*m_toolbar, view().toggle_bookmark_action()));
    m_toolbar->addAction(create_application_action(*m_toolbar, view().reset_zoom_action()));
    m_hamburger_button_action = m_toolbar->addWidget(m_hamburger_button);

    m_toolbar->setIconSize({ 16, 16 });
    m_toolbar->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
    // This is a little awkward, but without this Qt shrinks the button to the size of the icon.
    // Note: toolButtonStyle="0" -> ToolButtonIconOnly.
    m_toolbar->setStyleSheet("QToolButton[toolButtonStyle=\"0\"]{width:24px;height:24px}");

    m_hamburger_button_action->setVisible(!Settings::the()->show_menubar());

    QObject::connect(Settings::the(), &Settings::show_menubar_changed, this, [this](bool show_menubar) {
        m_hamburger_button_action->setVisible(!show_menubar);
    });

    view().on_activate_tab = [this] {
        m_window->activate_tab(tab_index());
    };

    view().on_close = [this] {
        m_window->definitely_close_tab(tab_index());
    };

    view().on_link_hover = [this](auto const& url) {
        m_hover_label->setText(qstring_from_ak_string(url.to_byte_string()));
        update_hover_label();
        m_hover_label->show();
    };

    view().on_link_unhover = [this]() {
        m_hover_label->hide();
    };

    view().on_load_start = [this](URL::URL const& url, bool) {
        ++m_geolocation_navigation_generation;
        if (m_geolocation_navigation_generation == 0u)
            ++m_geolocation_navigation_generation;
        if (m_geolocation_dialog)
            m_geolocation_dialog->close();
        auto url_serialized = qstring_from_ak_string(url.serialize());

        m_title = url_serialized;
        emit title_changed(tab_index(), url_serialized);

        m_favicon = default_favicon();
        emit favicon_changed(tab_index(), m_favicon);

        m_location_edit->set_url(url);
        m_location_edit->setCursorPosition(0);
    };

    view().on_request_geolocation_permission = [this](ByteString origin, u32 timeout_ms) {
        auto request_id = ++m_next_geolocation_request_id;
        if (request_id == 0u)
            return WebView::ViewImplementation::PermissionRequest {};
        auto navigation_generation = m_geolocation_navigation_generation;
        QTimer::singleShot(0, this, [this, origin = move(origin), timeout_ms,
                                    navigation_generation, request_id] {
            request_geolocation_permission(move(origin), timeout_ms,
                                           navigation_generation, request_id);
        });
        return WebView::ViewImplementation::PermissionRequest {
            navigation_generation, request_id };
    };

    view().on_url_change = [this](auto const& url) {
        m_location_edit->set_url(url);
    };

    QObject::connect(m_location_edit, &QLineEdit::returnPressed, this, &Tab::location_edit_return_pressed);

    view().on_title_change = [this](auto const& title) {
        m_title = qstring_from_utf16_string(title);
        emit title_changed(tab_index(), m_title);
    };

    view().on_favicon_change = [this](auto const& bitmap) {
        auto qimage = QImage(bitmap.scanline_u8(0), bitmap.width(), bitmap.height(), QImage::Format_ARGB32);
        if (qimage.isNull())
            return;
        auto qpixmap = QPixmap::fromImage(qimage);
        if (qpixmap.isNull())
            return;

        m_favicon = qpixmap;
        emit favicon_changed(tab_index(), m_favicon);
    };

    view().on_request_alert = [this](auto const& message) {
        m_dialog = new QMessageBox(QMessageBox::Icon::Warning, "Ladybird", qstring_from_ak_string(message), QMessageBox::StandardButton::Ok, &view());

        QObject::connect(m_dialog, &QDialog::finished, this, [this]() {
            view().alert_closed();
            m_dialog = nullptr;
        });

        m_dialog->open();
    };

    view().on_request_confirm = [this](auto const& message) {
        m_dialog = new QMessageBox(QMessageBox::Icon::Question, "Ladybird", qstring_from_ak_string(message), QMessageBox::StandardButton::Ok | QMessageBox::StandardButton::Cancel, &view());

        QObject::connect(m_dialog, &QDialog::finished, this, [this](auto result) {
            view().confirm_closed(result == QMessageBox::StandardButton::Ok || result == QDialog::Accepted);
            m_dialog = nullptr;
        });

        m_dialog->open();
    };

    view().on_request_prompt = [this](auto const& message, auto const& default_) {
        m_dialog = new QInputDialog(&view());

        auto& dialog = static_cast<QInputDialog&>(*m_dialog);
        dialog.setWindowTitle("Ladybird");
        dialog.setLabelText(qstring_from_ak_string(message));
        dialog.setTextValue(qstring_from_ak_string(default_));

        QObject::connect(m_dialog, &QDialog::finished, this, [this](auto result) {
            if (result == QDialog::Accepted) {
                auto& dialog = static_cast<QInputDialog&>(*m_dialog);
                view().prompt_closed(ak_string_from_qstring(dialog.textValue()));
            } else {
                view().prompt_closed({});
            }

            m_dialog = nullptr;
        });

        m_dialog->open();
    };

    view().on_request_set_prompt_text = [this](auto const& message) {
        if (m_dialog && is<QInputDialog>(*m_dialog))
            static_cast<QInputDialog&>(*m_dialog).setTextValue(qstring_from_ak_string(message));
    };

    view().on_request_accept_dialog = [this]() {
        if (m_dialog)
            m_dialog->accept();
    };

    view().on_request_dismiss_dialog = [this]() {
        if (m_dialog)
            m_dialog->reject();
    };

    view().on_request_color_picker = [this](Color current_color) {
        m_dialog = new QColorDialog(QColor(current_color.red(), current_color.green(), current_color.blue()), &view());

        auto& dialog = static_cast<QColorDialog&>(*m_dialog);
        dialog.setWindowTitle("Ladybird");
        dialog.setOption(QColorDialog::ShowAlphaChannel, false);
        QObject::connect(&dialog, &QColorDialog::currentColorChanged, this, [this](QColor const& color) {
            view().color_picker_update(Color(color.red(), color.green(), color.blue()), Web::HTML::ColorPickerUpdateState::Update);
        });

        QObject::connect(m_dialog, &QDialog::finished, this, [this](auto result) {
            if (result == QDialog::Accepted) {
                auto& dialog = static_cast<QColorDialog&>(*m_dialog);
                view().color_picker_update(Color(dialog.selectedColor().red(), dialog.selectedColor().green(), dialog.selectedColor().blue()), Web::HTML::ColorPickerUpdateState::Closed);
            } else {
                view().color_picker_update({}, Web::HTML::ColorPickerUpdateState::Closed);
            }

            m_dialog = nullptr;
        });

        m_dialog->open();
    };

    view().on_request_file_picker = [this](auto const& accepted_file_types, auto allow_multiple_files) {
        Vector<Web::HTML::SelectedFile> selected_files;

        auto create_selected_file = [&](auto const& qfile_path) {
            auto file_path = ak_byte_string_from_qstring(qfile_path);

            if (auto file = Web::HTML::SelectedFile::from_file_path(file_path); file.is_error())
                warnln("Unable to open file {}: {}", file_path, file.error());
            else
                selected_files.append(file.release_value());
        };

        QStringList accepted_file_filters;
        QMimeDatabase mime_database;

        for (auto const& filter : accepted_file_types.filters) {
            filter.visit(
                [&](Web::HTML::FileFilter::FileType type) {
                    QString title;
                    QString filter;

                    switch (type) {
                    case Web::HTML::FileFilter::FileType::Audio:
                        title = "Audio files";
                        filter = "audio/";
                        break;
                    case Web::HTML::FileFilter::FileType::Image:
                        title = "Image files";
                        filter = "image/";
                        break;
                    case Web::HTML::FileFilter::FileType::Video:
                        title = "Video files";
                        filter = "video/";
                        break;
                    }

                    QStringList extensions;

                    for (auto const& mime_type : mime_database.allMimeTypes()) {
                        if (mime_type.name().startsWith(filter))
                            extensions.append(mime_type.globPatterns());
                    }

                    accepted_file_filters.append(QString("%1 (%2)").arg(title, extensions.join(" ")));
                },
                [&](Web::HTML::FileFilter::MimeType const& filter) {
                    if (auto mime_type = mime_database.mimeTypeForName(qstring_from_ak_string(filter.value)); mime_type.isValid())
                        accepted_file_filters.append(mime_type.filterString());
                },
                [&](Web::HTML::FileFilter::Extension const& filter) {
                    auto extension = MUST(String::formatted("*.{}", filter.value));
                    accepted_file_filters.append(qstring_from_ak_string(extension));
                });
        }

        accepted_file_filters.size() > 1 ? accepted_file_filters.prepend("All files (*)") : accepted_file_filters.append("All files (*)");
        auto filters = accepted_file_filters.join(";;");

        if (allow_multiple_files == Web::HTML::AllowMultipleFiles::Yes) {
            auto paths = QFileDialog::getOpenFileNames(this, "Select files", QDir::homePath(), filters);
            selected_files.ensure_capacity(static_cast<size_t>(paths.size()));

            for (auto const& path : paths)
                create_selected_file(path);
        } else {
            auto path = QFileDialog::getOpenFileName(this, "Select file", QDir::homePath(), filters);
            create_selected_file(path);
        }

        view().file_picker_closed(std::move(selected_files));
    };

    view().on_find_in_page = [this](auto current_match_index, auto const& total_match_count) {
        m_find_in_page->update_result_label(current_match_index, total_match_count);
    };

    QObject::connect(focus_location_editor_action, &QAction::triggered, this, &Tab::focus_location_editor);

    view().on_restore_window = [this]() {
        m_window->showNormal();
    };

    view().on_reposition_window = [this](auto const& position) {
        m_window->move(position.x(), position.y());
        view().did_update_window_rect();
    };

    view().on_resize_window = [this](auto const& size) {
        m_window->resize(size.width(), size.height());
        view().did_update_window_rect();
    };

    view().on_maximize_window = [this]() {
        m_window->showMaximized();
        view().did_update_window_rect();
    };

    view().on_minimize_window = [this]() {
        m_window->showMinimized();
    };

    view().on_fullscreen_window = [this]() {
        m_toolbar->hide();
        m_window->fullscreen_mode().enter(this);
    };

    view().on_exit_fullscreen_window = [this]() {
        m_window->fullscreen_mode().exit(FullscreenMode::ExitInitiatedBy::WebContent);
        m_toolbar->show();
    };

    view().on_audio_play_state_changed = [this](auto play_state) {
        emit audio_play_state_changed(tab_index(), play_state);
    };

    auto* duplicate_tab_action = new QAction("&Duplicate Tab", this);
    QObject::connect(duplicate_tab_action, &QAction::triggered, this, [this]() {
        m_window->new_tab_from_url(view().url(), Web::HTML::ActivateTab::Yes);
    });

    auto* move_to_start_action = new QAction("Move to &Start", this);
    QObject::connect(move_to_start_action, &QAction::triggered, this, [this]() {
        m_window->move_tab(tab_index(), 0);
    });

    auto* move_to_end_action = new QAction("Move to &End", this);
    QObject::connect(move_to_end_action, &QAction::triggered, this, [this]() {
        m_window->move_tab(tab_index(), m_window->tab_count() - 1);
    });

    auto* close_tab_action = new QAction("&Close Tab", this);
    QObject::connect(close_tab_action, &QAction::triggered, this, [this]() {
        request_close();
    });

    auto* close_tabs_to_left_action = new QAction("C&lose Tabs to Left", this);
    QObject::connect(close_tabs_to_left_action, &QAction::triggered, this, [this]() {
        for (auto i = tab_index() - 1; i >= 0; i--) {
            m_window->request_to_close_tab(i);
        }
    });

    auto* close_tabs_to_right_action = new QAction("Close Tabs to R&ight", this);
    QObject::connect(close_tabs_to_right_action, &QAction::triggered, this, [this]() {
        for (auto i = m_window->tab_count() - 1; i > tab_index(); i--) {
            m_window->request_to_close_tab(i);
        }
    });

    auto* close_other_tabs_action = new QAction("Cl&ose Other Tabs", this);
    QObject::connect(close_other_tabs_action, &QAction::triggered, this, [this]() {
        for (auto i = m_window->tab_count() - 1; i >= 0; i--) {
            if (i == tab_index())
                continue;

            m_window->request_to_close_tab(i);
        }
    });

    m_context_menu = new QMenu("Context menu", this);
    m_context_menu->addAction(create_application_action(*this, WebView::Application::the().reload_action()));
    m_context_menu->addAction(duplicate_tab_action);
    m_context_menu->addSeparator();
    auto* move_tab_menu = m_context_menu->addMenu("Mo&ve Tab");
    move_tab_menu->addAction(move_to_start_action);
    move_tab_menu->addAction(move_to_end_action);
    m_context_menu->addSeparator();
    m_context_menu->addAction(close_tab_action);
    auto* close_multiple_tabs_menu = m_context_menu->addMenu("Close &Multiple Tabs");
    close_multiple_tabs_menu->addAction(close_tabs_to_left_action);
    close_multiple_tabs_menu->addAction(close_tabs_to_right_action);
    close_multiple_tabs_menu->addAction(close_other_tabs_action);
}

Tab::~Tab() = default;

void Tab::focus_location_editor()
{
    m_location_edit->setFocus();
    m_location_edit->selectAll();
}

void Tab::navigate(URL::URL const& url)
{
    view().load(url);
}

void Tab::load_html(StringView html)
{
    view().load_html(html);
}

void Tab::location_edit_return_pressed()
{
    if (m_location_edit->text().isEmpty())
        return;
    navigate(m_location_edit->url());
}

void Tab::open_file()
{
    auto filename = QFileDialog::getOpenFileUrl(this, "Open file", QDir::homePath(), "All Files (*.*)");
    if (filename.isValid()) {
        navigate(ak_url_from_qurl(filename));
    }
}

int Tab::tab_index()
{
    return m_window->tab_index(this);
}

void Tab::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (m_hover_label->isVisible())
        update_hover_label();
}

void Tab::update_hover_label()
{
    m_hover_label->setText(QFontMetrics(m_hover_label->font()).elidedText(m_hover_label->text(), Qt::ElideRight, width() / 2 - 10));
    m_hover_label->resize(QFontMetrics(m_hover_label->font()).boundingRect(m_hover_label->text()).adjusted(-4, -2, 4, 2).size());

    auto hover_label_height = height() - m_hover_label->height();
    if (m_find_in_page->isVisible())
        hover_label_height -= m_find_in_page->height();

    if (m_hover_label->underMouse() && m_hover_label->x() == 0)
        m_hover_label->move(width() / 2 + (width() / 2 - m_hover_label->width()), hover_label_height);
    else
        m_hover_label->move(0, hover_label_height);

    m_hover_label->raise();
}

bool Tab::event(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange) {
        recreate_toolbar_icons();
        return QWidget::event(event);
    }

    return QWidget::event(event);
}

void Tab::recreate_toolbar_icons()
{
    m_navigate_back_action->setIcon(create_tvg_icon_with_theme_colors("back", palette()));
    m_navigate_forward_action->setIcon(create_tvg_icon_with_theme_colors("forward", palette()));
    m_reload_action->setIcon(create_tvg_icon_with_theme_colors("reload", palette()));
    m_window->new_tab_action().setIcon(create_tvg_icon_with_theme_colors("new_tab", palette()));
    m_hamburger_button->setIcon(create_tvg_icon_with_theme_colors("hamburger", palette()));
}

void Tab::show_find_in_page()
{
    m_find_in_page->setVisible(true);
    m_find_in_page->setFocus();
}

void Tab::find_previous()
{
    m_find_in_page->find_previous();
}

void Tab::find_next()
{
    m_find_in_page->find_next();
}

void Tab::request_close()
{
    // Prevent closing on first request so WebContent can cleanly shutdown (e.g. asking if the user is sure they want
    // to leave, closing WebSocket connections, etc.)
    if (!m_already_requested_close) {
        m_already_requested_close = true;
        view().request_close();
        return;
    }

    // If the user has already requested a close, then respect the user's request and just close the tab.
    // For example, the WebContent process may not be responding.
    m_window->definitely_close_tab(tab_index());
}

void Tab::request_geolocation_permission(ByteString origin, u32 timeout_ms,
    u32 navigation_generation, u64 request_id)
{
    if (navigation_generation != m_geolocation_navigation_generation)
        return;
    auto canonical_origin = canonical_geolocation_origin(origin);
    if (!canonical_origin.has_value()) {
        view().complete_geolocation_permission(
            navigation_generation, request_id, false, false, 0, 0, 0, 0, 0,
            0, 0, 0);
        return;
    }

    if (auto decision = Settings::the()->geolocation_permission(*canonical_origin); decision.has_value()) {
        if (!decision.value()) {
            view().complete_geolocation_permission(
                navigation_generation, request_id, false, false, 0, 0, 0, 0,
                0, 0, 0, 0);
            return;
        }
        request_geolocation_position(timeout_ms, navigation_generation, request_id);
        return;
    }

    auto* dialog = new QMessageBox(
        QMessageBox::Icon::Question, "Location access",
        QStringLiteral("%1 wants to access your location.").arg(*canonical_origin),
        QMessageBox::StandardButton::NoButton, this);
    auto* allow_button = dialog->addButton("Allow", QMessageBox::ButtonRole::AcceptRole);
    auto* block_button = dialog->addButton("Block", QMessageBox::ButtonRole::RejectRole);
    dialog->setDefaultButton(block_button);
    dialog->setEscapeButton(block_button);
    dialog->setAttribute(Qt::WidgetAttribute::WA_DeleteOnClose);
    m_geolocation_dialog = dialog;
    QObject::connect(dialog, &QMessageBox::finished, this,
        [this, dialog, allow_button, canonical_origin = *canonical_origin,
         timeout_ms, navigation_generation, request_id] {
            if (navigation_generation != m_geolocation_navigation_generation)
                return;
            m_geolocation_dialog = nullptr;
            auto allowed = dialog->clickedButton() == allow_button;
            if (!Settings::the()->set_geolocation_permission(canonical_origin, allowed))
                allowed = false;
            if (!allowed) {
                view().complete_geolocation_permission(
                    navigation_generation, request_id, false, false, 0, 0, 0,
                    0, 0, 0, 0, 0);
                return;
            }
            request_geolocation_position(timeout_ms, navigation_generation, request_id);
        });
    dialog->open();
}

void Tab::request_geolocation_position(u32 timeout_ms,
    u32 navigation_generation, u64 request_id)
{
    if (navigation_generation != m_geolocation_navigation_generation)
        return;
    auto* source = QGeoPositionInfoSource::createDefaultSource(this);
    if (!source) {
        view().complete_geolocation_permission(
            navigation_generation, request_id, true, false, 0, 0, 0, 0, 0,
            0, 0, 0);
        return;
    }

    auto source_guard = QPointer<QGeoPositionInfoSource>(source);
    auto completed = std::make_shared<bool>(false);
    auto finish = [this, source_guard, completed, navigation_generation,
                   request_id](bool has_fix, i64 latitude_e7, i64 longitude_e7,
                   i64 altitude_mm, u32 horizontal_accuracy_mm,
                   u32 vertical_accuracy_mm, u32 speed_mm_per_second,
                   u32 heading_millidegrees, u32 flags) {
        if (*completed)
            return;
        *completed = true;
        if (source_guard) {
            source_guard->stopUpdates();
            source_guard->deleteLater();
        }
        view().complete_geolocation_permission(
            navigation_generation, request_id, true, has_fix, latitude_e7,
            longitude_e7, altitude_mm, horizontal_accuracy_mm,
            vertical_accuracy_mm, speed_mm_per_second,
            heading_millidegrees, flags);
    };

    QObject::connect(source, &QGeoPositionInfoSource::positionUpdated, this,
        [finish](QGeoPositionInfo const& info) {
            auto coordinate = info.coordinate();
            auto latitude = coordinate.latitude();
            auto longitude = coordinate.longitude();
            if (!coordinate.isValid() || !std::isfinite(latitude) ||
                !std::isfinite(longitude) || latitude < -90.0 || latitude > 90.0 ||
                longitude < -180.0 || longitude > 180.0 ||
                !info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy))
                return;

            auto horizontal_accuracy = info.attribute(
                QGeoPositionInfo::HorizontalAccuracy);
            auto max_accuracy_m = static_cast<double>(std::numeric_limits<u32>::max()) / 1000.0;
            if (!std::isfinite(horizontal_accuracy) || horizontal_accuracy <= 0.0 ||
                horizontal_accuracy > max_accuracy_m)
                return;

            auto horizontal_accuracy_mm = static_cast<u32>(
                std::ceil(horizontal_accuracy * 1000.0));
            if (horizontal_accuracy_mm == 0u)
                return;

            i64 altitude_mm = 0;
            u32 vertical_accuracy_mm = 0;
            u32 speed_mm_per_second = 0;
            u32 heading_millidegrees = 0;
            u32 flags = 0;

            auto altitude = coordinate.altitude();
            if (std::isfinite(altitude) &&
                info.hasAttribute(QGeoPositionInfo::VerticalAccuracy)) {
                auto vertical_accuracy = info.attribute(
                    QGeoPositionInfo::VerticalAccuracy);
                if (std::isfinite(vertical_accuracy) && vertical_accuracy > 0.0 &&
                    vertical_accuracy <= max_accuracy_m &&
                    std::abs(altitude) <= static_cast<double>(std::numeric_limits<i64>::max()) / 1000.0) {
                    altitude_mm = static_cast<i64>(std::llround(altitude * 1000.0));
                    vertical_accuracy_mm = static_cast<u32>(
                        std::ceil(vertical_accuracy * 1000.0));
                    if (vertical_accuracy_mm != 0u)
                        flags |= 1u;
                    else
                        altitude_mm = 0;
                }
            }

            if (info.hasAttribute(QGeoPositionInfo::GroundSpeed)) {
                auto speed = info.attribute(QGeoPositionInfo::GroundSpeed);
                if (std::isfinite(speed) && speed >= 0.0 && speed <= max_accuracy_m) {
                    speed_mm_per_second = static_cast<u32>(std::llround(speed * 1000.0));
                    flags |= 2u;
                }
            }

            if (info.hasAttribute(QGeoPositionInfo::Direction)) {
                auto heading = info.attribute(QGeoPositionInfo::Direction);
                if (std::isfinite(heading) && heading >= 0.0 && heading <= 360.0) {
                    auto normalized_heading = std::fmod(heading, 360.0);
                    heading_millidegrees = static_cast<u32>(
                        std::llround(normalized_heading * 1000.0));
                    if (heading_millidegrees >= 360000u)
                        heading_millidegrees = 0u;
                    flags |= 4u;
                }
            }

            finish(true,
                static_cast<i64>(std::llround(latitude * 10000000.0)),
                static_cast<i64>(std::llround(longitude * 10000000.0)),
                altitude_mm, horizontal_accuracy_mm, vertical_accuracy_mm,
                speed_mm_per_second, heading_millidegrees, flags);
        });
    QObject::connect(source, &QGeoPositionInfoSource::errorOccurred, this,
        [finish](QGeoPositionInfoSource::Error) {
            finish(false, 0, 0, 0, 0, 0, 0, 0, 0);
        });

    constexpr u32 provider_grace_ms = 1'000u;
    constexpr u32 maximum_provider_wait_ms = 120'000u;
    auto requested_provider_timeout = timeout_ms == 0xFFFFFFFFu
        ? maximum_provider_wait_ms
        : (timeout_ms > maximum_provider_wait_ms - provider_grace_ms
            ? maximum_provider_wait_ms
            : timeout_ms + provider_grace_ms);
    auto provider_timeout_ms = static_cast<int>(requested_provider_timeout);
    source->requestUpdate(provider_timeout_ms);
}

}
