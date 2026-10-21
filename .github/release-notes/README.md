# Release notes

One file per release, named after the tag: `v0.3.0.md`. The first heading is
the release title, the rest is the release text.

```markdown
# NickelReadingStats v0.3.0

What changed, for users first.
```

Release flow:

1. Open a pull request that adds `.github/release-notes/<tag>.md` (and any
   version bumps), merge it to `main`.
2. Tag the merge commit and push the tag:
   `git tag -a v0.3.0 -m "NickelReadingStats v0.3.0" && git push origin v0.3.0`.
3. `release.yml` runs CI, cross-builds `Kobo.tgz` and publishes the release
   with `Kobo.tgz` and `SHA256SUMS`.

Releases up to v0.2.1 were published by hand on Codeberg and copied to
GitHub; their text is on the releases page.
