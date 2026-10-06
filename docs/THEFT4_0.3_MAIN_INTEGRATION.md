# Theft4 0.3 promotion preparation

Candidate: **0.3.0 (123)**, launcher **0.3**, official bundle
`com.lukebrosious.theft4`, branch `codex/direct-metal-96`.
Status: prepare and test the candidate; **main has not been changed and no public
release has been published**.

## Source and release scope

- Local main is build 94, `b6b4a823403ff38623ce3a46bab369c858c21251`.
- Remote main was checked during preparation: `f5f30b373fc5b397364b81fb840f1d638267c14f`.
  Its change from the shared public base is the restored README donation block.
  The candidate preserves that block. Integrate the remote commit into the isolated
  candidate before its final build so promotion preserves its history as well.
- The branch contains 79 development commits through installed build 121, plus
  the 0.3 candidate pass. [The change manifest](THEFT4_0.3_CHANGE_MANIFEST.json)
  inventories the changes; [release notes](RELEASE_0.3.md) describe users' changes,
  and [architecture](THEFT4_0.3_ARCHITECTURE.md) separates upstream foundations,
  previous Theft4 work and direct Metal rewrites.
- The version metadata is changed, but that does not publish or tag a release.
  The final built app and signed-artifact manifest identify its exact commit.

## Main checkout preservation

Main contains uncommitted logging/cache experiments, tests, source files and
investigation documents. Its changed files have been fingerprinted and its
tracked patch saved privately with the candidate artifact. This preparation does
not discard, stage, commit or reset that checkout.

The candidate overlaps local changes in **README.md** and **.gitignore**. The
README donation block is already included in the candidate. Local `.gitignore`
exemptions for the transition investigation and ledger must be retained when
reconciling the files. Other main experiments are separate from the tested Metal
candidate; do not silently fold them into the release or report their behavior
as already tested.

Before changing main, preserve its owner changes in a reviewed checkpoint or
recoverable patch/untracked-file backup. Check the candidate against that source
state, reconcile README/ignore rules, then promote without overwriting unrelated
work. Re-run relevant checks if the resulting release tree includes additional
main experiments. Avoid a force checkout, hard reset or blanket staging.

## Candidate verification

Required before promotion:

1. Optimized signed ARM64 Release build with both direct Metal options On,
   official identity, version 0.3.0/build 123, and private asset draw capture Off.
2. The CPU contracts, including the new bounded index-cache test; Diagnostic and
   Retail GPU validation; all 32 saved draw replays; worker/upload lifetime checks.
3. Exact source provenance, strict signature verification, expected entitlements
   and unchanged stock/host shader libraries. User files and prepared texture
   caches are retained during in-place device installation.
4. M5 gameplay on the same route/settings used for build 121. Check initial spike
   recovery, heavy turns, image correctness and continued cache reuse. Ordinary
   play automatically uses the optimized policy; overlays are independent and a
   bounded capture can be enabled before Play without enabling development probes.
5. Separate non-M5/focus/long-session checks before claiming broad performance or
   universal console-equivalent stability. They are not implied by the M5 result.

The latest user-confirmed build 121 is a major improvement but still spikes and
recovers. Build 122's index change has CPU parity and saturation tests plus a
scoped Mac benchmark; its device performance requires the user's next run.
The remaining spike is disclosed in the release notes, not described as solved.

## Public distribution after source promotion

Both generator and public release helper now explicitly select direct Metal,
require offline game/host manifests and omit private draw capture. Tests with
fake build tools verify routing and rejection before packaging; they are not a
fresh-machine/full-IPA build validation.

After the final release tree is agreed, build an unsigned public IPA with
`tools/build_ios_release.sh 0.3.0`, following [the build guide](IOS_RELEASE_BUILD.md).
Run the existing architecture/privacy audit, verify required shader files, record
the IPA SHA-256 and validate signing/install in the intended distribution flow.
Keep game assets, title updates, user saves/caches, raw draw captures, device logs,
developer signing material and local source paths out of the public package.

Only then create the version tag and publish the corresponding notes/artifact
through the chosen release process. The current task prepares that result;
it has not pushed main, created a tag, uploaded an IPA or changed TestFlight.
