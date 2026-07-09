// Build stamp -- printed on every server boot (called from the mission init.c) so the running build can be
// confirmed from the RPT rather than a stale script cache / old PBO. Bump the version string on each build.
void ReconquestEvac_BuildStamp()
{
	Print( "[ReconquestEvac] =============== RCE EXTRACTION v1.0.0 loaded ===============" );
}
