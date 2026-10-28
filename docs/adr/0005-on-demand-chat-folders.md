# ADR 0005: On-demand Telegram chat folders and archive isolation

- **Status:** Accepted
- **Date:** 2026-10-28

## Context

The account contains several custom Telegram folders and an archive. Treating
all updates as one chat list both hid the user's folder structure and allowed
archived chats to trigger user, avatar, and media work while invisible. Chat
pagination and history must also not leak from one folder into another.

The device has limited CPU, memory, and bandwidth. Loading an archive that the
user has not opened wastes all three and can delay the visible chats.

## Decision

Use explicit TDLib chat lists for each visible tab:

- **All** requests `chatListMain` and accepts only non-archived chats that have
  an explicit `chatListMain` membership.
- A custom folder requests `chatListFolder` with its `chat_folder_id` and
  accepts only matching, non-archived chats.
- **Archive** is the last tab, after all folders returned by
  `updateChatFolders`, and requests `chatListArchive`.

Filter incoming chat updates before caching the chat or requesting its user,
avatar, or media. Archived updates are ignored in every non-archive tab, even
when the chat also belongs to a custom folder. Chat history and media are
loaded on demand; the archive does not get a background preload.

Changing tabs clears the selected chat, cached visible list, message history,
and pagination/exhaustion state. Left and Right move between All, custom
folders, and Archive. The UI may scroll the tab strip to keep the selected tab
visible on the 640-pixel screen.

## Consequences

- The user's actual TDLib folders are exposed rather than a hard-coded set of
  folder names.
- Opening Archive is an explicit opt-in to its chats and subsequent downloads.
- Cached data is not deleted when changing tabs or leaving Archive.
- The current JSON tests exercise synthetic TDLib updates. They do not prove
  authorization or the folder contents of the user's live account.

## Validation

- Host and AArch64 `--telegram-json-test` runs verify list selection,
  filter-before-download behavior, archive opt-in, and pagination reset.
- The `--qr-test` and `--keyboard-test` device smoke tests remain green.
- Live-account verification still requires an authorized Telegram session:
  switch through every custom folder and Archive, then inspect downloads and
  logs without exposing account content.

## Related

- ADR 0001: TDLib JSON as the Telegram runtime
- ADR 0006: Short-lived uinput remote debugging
