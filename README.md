# Linux kernel for ia64

This is the Linux kernel part of the [EPIC Linux project](https://epic-linux.org),
serving as upstream for ia64 Linux kernels since its deletion in Linus' tree
in 6.7.

## Release and merge policy

To distinguish from Linus' mainline, releases have an additional suffix of
-epic[n]. Usually, the first RC is tagged with -epic1, and further versions are
tagged with -epic2, -epic3 and so on. Both directly commited ia64 chages and
merges from other trees bump up the epic number when tagged.

We try to merge any substantial changes from a separate branch. For efficiency
purposes, this excludes simple replays and reverts after merges from Linus'
tree. Any merge conflicts are described in the merge commit.

Usually, two merges from Linus' tree are done in a release cycle: one early
(usualy -rc1, sometimes later RC) - most breaking changes needing replays,
reverts, and sometimes migration to new APIs happen there, and one for
the final tagged release.

## Distributions

The following distributions take this kernel tree in one form or another:

- [T2 Linux](https://t2linux.com)
- [EPIC Slack](http://epic-slack.org)
- [AOSC Retro](https://wiki.aosc.io/aosc-os/retro)

## History

Up to 6.12, the tree was rebased on top of Linus' tree every once in a while
(not every release), which meant rewriting history and the ia64 patches changing
commit IDs every release, at the benefit of having a linear history with respect
to Linus' tree.

Starting with 6.16, this model was replaced by a regular merge model, where
Linus' tree is treated as just another downstream repo from where merges are
done. That means that the history is clean since the 6.12 rebase.

Up to 7.1, merges did not have a full description (inclusion of downstream tree
link and conflicts).
