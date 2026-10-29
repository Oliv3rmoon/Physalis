// Physalis - Plex as a source: signing in, its servers in Sources, and signing out.
// Copyright (C) 2026 Oliv3rmoon
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A Plex account signs in on the TV (tv/plex.hpp). Each of its servers becomes a source:
// its address, its access token in the source's password (where an Xtream password is kept),
// its machine identifier, and the account's name to show. The account's own token and this
// console's client identifier are in prosperotv-plex-v1.txt beside the sources. Neither the
// tokens nor the account's name go into a notice or the diagnostic log.

#include "tv/i18n.hpp"
#include "tv/model.hpp"

#include "tv/diag.hpp"
#include "tv/platform.hpp"

#include <algorithm>

namespace ptv
{
namespace
{
constexpr char kPlexFile[] = "prosperotv-plex-v1.txt";

std::string short_name(std::string name)
{
    if (name.size() > 128)
    {
        name.resize(128);
        while (!name.empty() && (static_cast<unsigned char>(name.back()) & 0xc0u) == 0x80u)
            name.pop_back();
        if (!name.empty() && static_cast<unsigned char>(name.back()) >= 0xc0u)
            name.pop_back();
    }
    for (char &c : name)
        if (static_cast<unsigned char>(c) < 0x20u || c == 0x7f)
            c = ' ';
    return name;
}
} // namespace

void Model::load_plex()
{
    if (!plex::load_account(path(kPlexFile), &plex_account_))
        plex_account_ = {};
    // Made once, kept from the first sign-in on: plex.tv lists this console by it.
    if (!plex::valid_client_id(plex_account_.client_id))
        plex_account_.client_id = plex::new_client_id();
    plex_identity_.client_id = plex_account_.client_id;
}

plex::Server Model::plex_server_for(const SavedSource &source) const
{
    plex::Server server;
    // Only an address this launch found answering for the server: nothing (no poster, no
    // list, no title, no token) goes to the saved address before then, or after it failed.
    if (const auto found = plex_bases_.find(source.id);
        found != plex_bases_.end() && !found->second.base.empty())
    {
        server.base = found->second.base;
        server.pin = found->second.pin;
    }
    server.token = source.password;
    server.machine = source.mac;
    server.name = source.name;
    server.source_id = plex::source_id(source.mac);
    return server;
}

int Model::plex_reach(std::int64_t id) const
{
    const auto found = plex_bases_.find(id);
    return found == plex_bases_.end() ? -1 : found->second.base.empty() ? 0 : 1;
}

void Model::configure_vod(const SavedSource &source)
{
    if (source.kind == 6)
    {
        // Not reached this launch: the saved lists alone, with nothing to send anywhere.
        auto server = plex_server_;
        if (!server.valid())
        {
            server.base.clear();
            server.pin.clear();
            server.token.clear();
        }
        vod_.configure_plex(std::move(server), plex_identity_, cache_path(active_source_),
                            schedule_);
        return;
    }
    vod_.configure(source.kind == 2 ? xtream_ : iptv::XtreamCredentials{},
                   cache_path(active_source_), schedule_, source.user_agent, source.referrer,
                   source.extra_headers);
    if (source.kind == 1)
        vod_.configure_playlist(&catalog_);
}

void Model::plex_sign_in()
{
    if (plex_sign_in_ || !require_parent())
        return;
    if (refreshing())
    {
        notify(Level::warning, tr("An update is running"),
               tr("Wait for it to finish, then choose a source."));
        return;
    }
    if (!plex::valid_client_id(plex_identity_.client_id))
        load_plex();
    plex_sign_in_ = std::make_unique<plex::SignIn>();
    // Signed in already: the account's servers are looked up again, with no code.
    if (!plex_sign_in_->start(plex_identity_, {}, plex_poll_ms_, plex_account_.token))
    {
        plex_sign_in_.reset();
        notify(Level::error, tr("Plex sign-in could not start"), tr("Try again."));
        return;
    }
    diag::event("plex: sign-in started");
    ++revision_;
}

void Model::plex_cancel_sign_in()
{
    if (!plex_sign_in_)
        return;
    plex_sign_in_->cancel();
    plex_sign_in_.reset();
    diag::event("plex: sign-in closed");
    ++revision_;
}

Model::PlexSignInView Model::plex_sign_in_view() const
{
    PlexSignInView view;
    if (!plex_sign_in_)
        return view;
    const auto step = plex_sign_in_->step();
    view.waiting = step == plex::SignIn::Step::waiting;
    view.finishing = step == plex::SignIn::Step::finishing || step == plex::SignIn::Step::done;
    if (view.waiting)
    {
        view.code = plex_sign_in_->code();
        view.seconds = plex_sign_in_->seconds_left();
    }
    return view;
}

void Model::poll_plex_sign_in()
{
    if (!plex_sign_in_ || !plex_sign_in_->finished())
        return;
    const auto job = std::move(plex_sign_in_);
    job->cancel(); // joins the finished worker
    ++revision_;
    switch (job->step())
    {
    case plex::SignIn::Step::done:
        commit_plex_sign_in(*job);
        break;
    case plex::SignIn::Step::failed:
        notify(Level::error, tr("Plex sign-in failed"), tr(job->error().c_str()));
        break;
    default:
        break;
    }
}

void Model::commit_plex_sign_in(const plex::SignIn &done)
{
    plex_account_.client_id = plex_identity_.client_id;
    // A name plex.tv did not give this time is kept, for the same account.
    if (!done.name().empty() || done.token() != plex_account_.token)
        plex_account_.name = short_name(done.name());
    plex_account_.token = done.token();
    if (!plex::save_account(path(kPlexFile), plex_account_))
        notify(Level::warning, tr("The Plex sign-in could not be saved"),
               tr("It holds until Physalis is closed."));
    if (!library_.ready())
    {
        notify(Level::error, tr("Library settings could not be opened"));
        return;
    }
    std::int64_t first = 0;
    unsigned unreached = 0;
    for (const auto &found : done.servers())
    {
        SavedSource source;
        bool existing = false;
        for (const auto &saved : sources_)
            if (saved.kind == 6 && saved.mac == found.resource.machine)
            {
                source = saved;
                existing = true;
                break;
            }
        // Only an address that answered for the server is ever saved: one that did not is
        // left for Find servers, and one kept from before says it cannot be reached.
        if (found.base.empty())
        {
            ++unreached;
            if (existing)
                plex_bases_[source.id] = {};
            continue;
        }
        source.kind = 6;
        source.name = short_name(found.resource.name.empty() ? "Plex" : found.resource.name);
        source.username = plex_account_.name;
        source.password = found.resource.token;
        source.mac = found.resource.machine;
        // A server kept from before keeps its address; where it is now is this launch's.
        if (!existing)
            source.url = found.base;
        if (!valid_source(source) || !library_.save_source(&source))
        {
            diag::event("plex: a server could not be saved as a source");
            continue;
        }
        plex_bases_[source.id] = {found.base, found.pin};
        if (!found.pin.empty())
            platform::pin_host(found.pin);
        if (!first)
            first = source.id;
    }
    sources_ = library_.sources();
    ++revision_;
    if (!first)
    {
        if (unreached)
            notify(Level::warning, tr("No Plex server could be reached"),
                   tr("Find servers adds them once they can be reached."));
        else
            notify(Level::warning, tr("No Plex server found"),
                   tr("This Plex account has no server this console can use."));
        return;
    }
    if (unreached)
        notify(Level::warning, tr("Some Plex servers could not be reached"),
               tr("Find servers adds them once they can be reached."));
    else
        notify(Level::ready, tr("Signed in to Plex"), tr("Your Plex servers are in Sources."));
    use_saved_source(first);
}

bool Model::plex_sign_out()
{
    if (!require_parent())
        return false;
    if (refreshing())
    {
        notify(Level::warning, tr("An update is running"),
               tr("Wait for it to finish, then choose a source."));
        return false;
    }
    plex_cancel_sign_in();
    const bool was_selected = active_source_ == iptv::SourceKind::Plex;
    // Overwritten where they lay, not only unlinked: the servers' tokens leave the file.
    const bool removed = library_.ready() && library_.remove_sources_securely(6);
    sources_ = library_.sources();
    plex_account_.token.clear();
    plex_account_.name.clear();
    plex_bases_.clear();
    plex_server_ = {};
    // Signed out from now on, and at the next start too: a file that keeps the token and
    // cannot be rewritten is removed (the console gets a new identifier next time).
    const auto file = path(kPlexFile);
    const bool saved = plex::save_account(file, plex_account_) || std::remove(file.c_str()) == 0;
    if (was_selected)
    {
        const SavedSource *next = nullptr;
        for (const auto &source : sources_)
            if (source.kind >= 1 && source.kind <= 6 && !source.url.empty())
            {
                next = &source;
                break;
            }
        if (next)
            use_saved_source(next->id);
        else if (const auto *none = saved_source(1))
        {
            (void)library_.select_source(1);
            select_source_record(*none);
            load_cache();
            set_status(tr("No source"), Level::ready);
            set_source_text(tr("No channels yet"),
                            tr("Add a playlist or account on the Sources tab."));
        }
    }
    diag::event("plex: signed out");
    notify(removed && saved ? Level::ready : Level::warning, tr("Signed out of Plex"),
           tr("Its servers were removed from Sources."));
    ++revision_;
    return removed && saved;
}

void Model::consume_plex_check()
{
    const auto id = refresh_saved_source_id_;
    const unsigned slot = static_cast<unsigned>(iptv::SourceKind::Plex);
    const bool ok = pending_plex_.ok;
    plex_bases_[id] = ok ? plex::Reached{pending_plex_.base, pending_plex_.pin} : plex::Reached{};
    if (ok && !pending_plex_.pin.empty())
        platform::pin_host(pending_plex_.pin);
    diag::event("plex: server check %s", ok ? "found the server" : "did not reach the server");
    if (const auto *source = saved_source(id); source && source->kind == 6 && ok)
    {
        // A new token or name from plex.tv is kept; the address is this launch's alone.
        auto next = *source;
        if (plex::valid_token(pending_plex_.token))
            next.password = pending_plex_.token;
        if (!pending_plex_.name.empty())
            next.name = short_name(pending_plex_.name);
        if ((next.password != source->password || next.name != source->name) &&
            valid_source(next) && library_.save_source(&next))
            sources_ = library_.sources();
    }
    if (const auto *source = saved_source(id);
        id == selected_source_id_ && source && source->kind == 6)
    {
        plex_server_ = plex_server_for(*source);
        configure_vod(*source);
    }
    health_[slot] = ok ? SourceHealth::ready : SourceHealth::error;
    if (ok)
    {
        set_status(tr("Plex"), Level::ready);
        set_source_text(plex_server_.name, tr("Movies and TV shows are on the On demand tab."));
    }
    else
    {
        const std::string problem =
            tr(pending_plex_.error.empty() ? "The Plex server could not be reached."
                                           : pending_plex_.error.c_str());
        set_status(tr("Not reachable"), Level::warning);
        set_source_text(tr("The Plex server could not be reached"),
                        problem + tr(" Saved titles stay available."));
        notify(Level::warning, tr("The Plex server could not be reached"), problem);
    }
    pending_plex_ = {};
    ++revision_;
}

Model::ImageCredentials Model::image_credentials() const
{
    if (active_source_ != iptv::SourceKind::Plex || !plex_server_.valid() ||
        !plex::valid_token(plex_server_.token))
        return {};
    return {plex_server_.base, {"X-Plex-Token: " + plex_server_.token}};
}

} // namespace ptv
