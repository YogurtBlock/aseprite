// Aseprite
// Copyright (C) 2018-2019  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "app/context.h"

#include "app/active_site_handler.h"
#include "app/app.h"
#include "app/commands/command.h"
#include "app/commands/commands.h"
#include "app/console.h"
#include "app/doc.h"
#include "app/site.h"
#include "base/scoped_value.h"
#include "doc/layer.h"
#include "ui/system.h"

#include <algorithm>
#include <stdexcept>

namespace app {

Context::Context()
  : m_docs(this)
  , m_lastSelectedDoc(nullptr)
  , m_transaction(nullptr)
{
  m_docs.add_observer(this);
}

Context::~Context()
{
  m_docs.remove_observer(this);
}

void Context::sendDocumentToTop(Doc* document)
{
  ASSERT(document != NULL);

  documents().move(document, 0);
}

void Context::closeDocument(Doc* doc)
{
  onCloseDocument(doc);
}

Site Context::activeSite() const
{
  Site site;
  onGetActiveSite(&site);
  return site;
}

Doc* Context::activeDocument() const
{
  Site site;
  onGetActiveSite(&site);
  return site.document();
}

void Context::setActiveDocument(Doc* document)
{
  onSetActiveDocument(document);
}

void Context::setActiveLayer(doc::Layer* layer)
{
  onSetActiveLayer(layer);
}

void Context::setActiveFrame(const doc::frame_t frame)
{
  onSetActiveFrame(frame);
}

bool Context::hasModifiedDocuments() const
{
  for (auto doc : documents())
    if (doc->isModified())
      return true;
  return false;
}

void Context::notifyActiveSiteChanged()
{
  Site site = activeSite();
  notify_observers<const Site&>(&ContextObserver::onActiveSiteChange, site);
}

void Context::executeCommandFromMenuOrShortcut(Command* command, const Params& params)
{
  ui::assert_ui_thread();

  // With this we avoid executing a command when we are inside another
  // command (e.g. if we press Cmd-S quickly the program can enter two
  // times in the File > Save command and hang).
  static Command* executingCommand = nullptr;
  if (executingCommand) {         // Ignore command execution
    LOG(VERBOSE, "CTXT: Ignoring command %s because we are inside %s\n",
        command->id().c_str(), executingCommand->id().c_str());
    return;
  }
  base::ScopedValue<Command*> commandGuard(executingCommand,
                                           command, nullptr);

  executeCommand(command, params);
}

void Context::executeCommand(Command* command, const Params& params)
{
  ASSERT(command);
  if (!command)
    return;

  Console console;
  LOG(VERBOSE, "CTXT: Executing command %s\n", command->id().c_str());
  try {
    m_flags.update(this);

    ASSERT(!command->needsParams() || !params.empty());

    command->loadParams(params);

    CommandExecutionEvent ev(command);
    BeforeCommandExecution(ev);

    if (ev.isCanceled()) {
      LOG(VERBOSE, "CTXT: Command %s was canceled/simulated.\n", command->id().c_str());
    }
    else if (command->isEnabled(this)) {
      command->execute(this);
      LOG(VERBOSE, "CTXT: Command %s executed successfully\n", command->id().c_str());
    }
    else {
      LOG(VERBOSE, "CTXT: Command %s is disabled\n", command->id().c_str());
    }

    AfterCommandExecution(ev);

    // TODO move this code to another place (e.g. a Workplace/Tabs widget)
    if (isUIAvailable())
      app_rebuild_documents_tabs();
  }
  catch (base::Exception& e) {
    LOG(ERROR, "CTXT: Exception caught executing %s command\n%s\n",
        command->id().c_str(), e.what());
    Console::showException(e);
  }
  catch (std::exception& e) {
    LOG(ERROR, "CTXT: std::exception caught executing %s command\n%s\n",
        command->id().c_str(), e.what());
    console.printf("An error ocurred executing the command.\n\nDetails:\n%s", e.what());
  }
#ifdef NDEBUG
  catch (...) {
    LOG(ERROR, "CTXT: Unknown exception executing %s command\n",
        command->id().c_str());

    console.printf("An unknown error ocurred executing the command.\n"
                   "Please save your work, close the program, try it\n"
                   "again, and report this bug.\n\n"
                   "Details: Unknown exception caught. This can be bad\n"
                   "memory access, divison by zero, etc.");
  }
#endif
}

void Context::onAddDocument(Doc* doc)
{
  m_lastSelectedDoc = doc;

  if (m_activeSiteHandler)
    m_activeSiteHandler->addDoc(doc);
}

void Context::onRemoveDocument(Doc* doc)
{
  if (doc == m_lastSelectedDoc)
    m_lastSelectedDoc = nullptr;

  if (m_activeSiteHandler)
    m_activeSiteHandler->removeDoc(doc);
}

void Context::onGetActiveSite(Site* site) const
{
  // Default/dummy site (maybe for batch/command line mode)
  if (Doc* doc = m_lastSelectedDoc)
    activeSiteHandler()->getActiveSiteForDoc(doc, site);
}

void Context::onSetActiveDocument(Doc* doc)
{
  m_lastSelectedDoc = doc;
}

void Context::onSetActiveLayer(doc::Layer* layer)
{
  Doc* newDoc = (layer ? static_cast<Doc*>(layer->sprite()->document()): nullptr);
  if (newDoc != m_lastSelectedDoc)
    setActiveDocument(newDoc);
  if (newDoc)
    activeSiteHandler()->setActiveLayerInDoc(newDoc, layer);
}

void Context::onSetActiveFrame(const doc::frame_t frame)
{
  if (m_lastSelectedDoc)
    activeSiteHandler()->setActiveFrameInDoc(m_lastSelectedDoc, frame);
}

void Context::setTransaction(Transaction* transaction)
{
  if (transaction) {
    ASSERT(!m_transaction);
    m_transaction = transaction;
  }
  else {
    ASSERT(m_transaction);
    m_transaction = nullptr;
  }
}

ActiveSiteHandler* Context::activeSiteHandler() const
{
  if (!m_activeSiteHandler)
    m_activeSiteHandler.reset(new ActiveSiteHandler);
  return m_activeSiteHandler.get();
}

void Context::onCloseDocument(Doc* doc)
{
  ASSERT(doc != nullptr);
  ASSERT(doc->context() == nullptr);
  delete doc;
}

} // namespace app
