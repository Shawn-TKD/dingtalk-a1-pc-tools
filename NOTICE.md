# Attribution and scope

This is an independent interoperability toolkit assembled from clean-room
experiments on hardware and accounts controlled by the contributors.

The research was informed by the public notes in
[AwHsR15/dingtalk-a1-reverse](https://github.com/AwHsR15/dingtalk-a1-reverse),
especially its `main` and `findings/h5-and-processing-architecture` branches.
That upstream repository did not publish a license at the time this toolkit
was prepared, so this repository does not copy its prose or claim that its
contents are MIT-licensed. Protocol facts are restated independently and the
runnable client code here was written and tested separately.

For reproducibility, `upstream/dingtalk-a1-reverse` is a Git submodule pinned
to upstream commit `62cbbf24bfc57badafb16cbc857e51ad8174ea5f` on
`findings/h5-and-processing-architecture`. The submodule remains a separate
work under its upstream author's ownership and is not covered by this
repository's MIT license. Clone with `--recurse-submodules` only if you want
the original research material locally; the toolkit itself does not require
the submodule at runtime.

"DingTalk", "钉钉", "TALIX", and other product names may be trademarks of
their respective owners. This project is not affiliated with or endorsed by
them.
