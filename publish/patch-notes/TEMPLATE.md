# 0.1.0

## Summary
Write the human part here. What changed, why it matters, what you want people to
try. A couple of short paragraphs reads better than one long one.

Blank lines separate paragraphs. Keep it conversational - this is the bit people
actually read.

## Fixed
- Fixed an issue where remote players crashed the client on spawn
- Fixed equipment not transferring to other players

## Added
- Per-launch log files, so a crash log is no longer wiped by relaunching

## Known issues
- Remote player movement speed is still reported incorrectly

<!--
  HOW THIS WORKS

  Filename doesn't matter. The "# 0.1.0" heading on the first line IS the version -
  it becomes the embed title ("Server Update - Patch 0.1.0") and the footer.

  "## Anything" becomes a bold section header. Name them whatever suits the release;
  Summary / Fixed / Added / Known issues is just a sensible default.

  "- item" becomes a bullet.

  Everything else is passed through as prose.

  Discord limits an embed description to 4096 characters. The script tells you the
  count and refuses to post something that would be silently truncated.

  Post it with:
      .\tools\AnnouncePatch.ps1 publish\patch-notes\your-file.md
  Preview without sending:
      .\tools\AnnouncePatch.ps1 publish\patch-notes\your-file.md -DryRun
-->
