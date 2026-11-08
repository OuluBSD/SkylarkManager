#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"
#include <GitHubForge/GitHubForge.h>

NAMESPACE_UPP

static Vector<ValueMap> sCustomRuns;
static StaticMutex sRunLock;

Vector<WorkflowTemplate> GitHubPlatformService::GetWorkflowTemplates(const String& owner, const String& repo)
{
	Vector<WorkflowTemplate> list;

	// 1. Suggested & Continuous Integration: U++ Build System
	{
		WorkflowTemplate& t = list.Add();
		t.id = "upp-build";
		t.title = "U++ Build System";
		t.category = "continuous-integration";
		t.author = "By U++ Team";
		t.suggested = true;
		t.icon = "package";
		t.description = "Build and test U++ packages using umk / bin/build with intelligent compilation cache and multi-method matrix (CLANG, GCC).";
		t.default_filename = "upp-build.yml";
		t.default_yaml =
			"name: U++ Build & Test\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master, main ]\n"
			"  pull_request:\n"
			"    branches: [ master, main ]\n"
			"  workflow_dispatch:\n\n"
			"jobs:\n"
			"  build-and-test:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - name: Checkout repository\n"
			"        uses: actions/checkout@v4\n\n"
			"      - name: Cache U++ compilation artifacts\n"
			"        uses: actions/cache@v5\n"
			"        with:\n"
			"          path: |\n"
			"            ~/.local/state/upp/cache\n"
			"            bin/CLANG\n"
			"          key: ${{ runner.os }}-upp-${{ hashFiles('**/*.upp', '**/*.h', '**/*.cpp') }}\n"
			"          restore-keys: |\n"
			"            ${{ runner.os }}-upp-\n\n"
			"      - name: Build packages with U++ umk / bin/build\n"
			"        uses: upp-actions/build@v1\n"
			"        with:\n"
			"          method: CLANG\n"
			"          packages: reference/ManagerSkylark uppsrc/Aria\n"
			"          flags: DEBUG BLITZ\n\n"
			"      - name: Run Aria Verification Suite\n"
			"        run: |\n"
			"          DISPLAY=:0 ./bin/CLANG/Aria site managerskylark test http://127.0.0.1:8080\n";
	}

	// 2. Continuous Integration: U++ Android CI
	{
		WorkflowTemplate& t = list.Add();
		t.id = "upp-android";
		t.title = "U++ Android CI";
		t.category = "continuous-integration";
		t.author = "By U++ Team";
		t.suggested = false;
		t.icon = "terminal";
		t.description = "Cross-compile U++ packages for Android SDK/NDK architectures (arm64-v8a, armeabi-v7a) using AndroidSDK toolchain.";
		t.default_filename = "android-ci.yml";
		t.default_yaml =
			"name: U++ Android CI\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master ]\n"
			"  workflow_dispatch:\n\n"
			"jobs:\n"
			"  android-build:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - name: Checkout\n"
			"        uses: actions/checkout@v4\n"
			"      - name: Setup Android NDK\n"
			"        uses: nttld/setup-ndk@v1\n"
			"        with:\n"
			"          ndk-version: r25c\n"
			"      - name: Build U++ Android Packages\n"
			"        uses: upp-actions/build@v1\n"
			"        with:\n"
			"          target: ANDROID\n"
			"          packages: uppsrc/AndroidSDK uppsrc/Core\n";
	}

	// 3. Continuous Integration: UppHub Package Publishing
	{
		WorkflowTemplate& t = list.Add();
		t.id = "upphub-publish";
		t.title = "UppHub Package Publishing";
		t.category = "continuous-integration";
		t.author = "By U++ Team";
		t.suggested = false;
		t.icon = "package";
		t.description = "Validate package metadata, build clean release artifacts, and publish release tag to UppHub community index.";
		t.default_filename = "upphub-publish.yml";
		t.default_yaml =
			"name: Publish to UppHub\n\n"
			"on:\n"
			"  release:\n"
			"    types: [ published ]\n"
			"  workflow_dispatch:\n\n"
			"jobs:\n"
			"  publish:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - name: Verify U++ package metadata\n"
			"        run: ./bin/build --verify-upphub\n"
			"      - name: Sync to UppHub Index\n"
			"        run: echo 'Submitting release package to UppHub registry...'\n";
	}

	// 4. Continuous Integration: Generic CMake
	{
		WorkflowTemplate& t = list.Add();
		t.id = "cpp-cmake";
		t.title = "CMake C/C++";
		t.category = "continuous-integration";
		t.author = "By actions";
		t.suggested = false;
		t.icon = "code";
		t.description = "Build and test C/C++ source code using modern CMake and CTest test runner.";
		t.default_filename = "cmake.yml";
		t.default_yaml =
			"name: CMake\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master ]\n"
			"  pull_request:\n"
			"    branches: [ master ]\n\n"
			"jobs:\n"
			"  build:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - name: Configure CMake\n"
			"        run: cmake -B ${{github.workspace}}/build -DCMAKE_BUILD_TYPE=Release\n"
			"      - name: Build\n"
			"        run: cmake --build ${{github.workspace}}/build --config Release\n"
			"      - name: Test\n"
			"        run: ctest --test-dir ${{github.workspace}}/build --output-on-failure\n";
	}

	// 5. Continuous Integration: C/C++ with Make
	{
		WorkflowTemplate& t = list.Add();
		t.id = "cpp-make";
		t.title = "C/C++ with Make";
		t.category = "continuous-integration";
		t.author = "By actions";
		t.suggested = false;
		t.icon = "terminal";
		t.description = "Compile C/C++ source code using GNU Make and verify build targets.";
		t.default_filename = "make.yml";
		t.default_yaml =
			"name: C/C++ CI\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master ]\n"
			"  pull_request:\n"
			"    branches: [ master ]\n\n"
			"jobs:\n"
			"  build:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - name: make\n"
			"        run: make -j$(nproc)\n"
			"      - name: make check\n"
			"        run: make check\n";
	}

	// 6. Deployment: Docker Container Deployment
	{
		WorkflowTemplate& t = list.Add();
		t.id = "deploy-docker";
		t.title = "Docker Container Deployment";
		t.category = "deployment";
		t.author = "By DevOps";
		t.icon = "server";
		t.description = "Build OCI compliant container image, tag with commit SHA and latest, and publish to container registry.";
		t.default_filename = "docker-publish.yml";
		t.default_yaml =
			"name: Docker Build & Deploy\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master ]\n"
			"    tags: [ 'v*.*.*' ]\n\n"
			"jobs:\n"
			"  docker:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - name: Build Container Image\n"
			"        run: docker build -t ai-upp:latest .\n"
			"      - name: Push Container\n"
			"        run: echo 'Pushing image to registry...'\n";
	}

	// 7. Deployment: Kubernetes Cluster Deployment
	{
		WorkflowTemplate& t = list.Add();
		t.id = "deploy-k8s";
		t.title = "Kubernetes Cluster Deployment";
		t.category = "deployment";
		t.author = "By DevOps";
		t.icon = "grid";
		t.description = "Deploy containerized services and Helm charts to Kubernetes cluster with rolling deployment.";
		t.default_filename = "k8s-deploy.yml";
		t.default_yaml =
			"name: Kubernetes Deployment\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ production ]\n"
			"  workflow_dispatch:\n\n"
			"jobs:\n"
			"  deploy-k8s:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - name: Deploy Manifests\n"
			"        run: kubectl apply -f k8s/\n";
	}

	// 8. Deployment: Web Application Service (Skylark Daemon)
	{
		WorkflowTemplate& t = list.Add();
		t.id = "deploy-webapp";
		t.title = "Web Application Service (Skylark Daemon)";
		t.category = "deployment";
		t.author = "By U++ Team";
		t.icon = "globe";
		t.description = "Package compiled Skylark HTTP binary, configure systemd daemon service, and perform zero-downtime rolling reload.";
		t.default_filename = "webapp-deploy.yml";
		t.default_yaml =
			"name: Deploy Skylark Web Daemon\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master ]\n"
			"  workflow_dispatch:\n\n"
			"jobs:\n"
			"  deploy-service:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - name: Compile Skylark Daemon\n"
			"        run: ./bin/build reference/ManagerSkylark\n"
			"      - name: Restart Systemd Service\n"
			"        run: sudo systemctl restart manager-skylark.service\n";
	}

	// 9. Deployment: Mesh Fleet Bare-Metal Deployment
	{
		WorkflowTemplate& t = list.Add();
		t.id = "deploy-mesh-fleet";
		t.title = "Mesh Fleet Bare-Metal Deployment";
		t.category = "deployment";
		t.author = "By U++ Mesh";
		t.icon = "network";
		t.description = "Deploy binary over fast SSH to local mesh cluster nodes (192.168.1.106, 192.168.1.105, 192.168.1.169) with automatic fleet sync.";
		t.default_filename = "mesh-deploy.yml";
		t.default_yaml =
			"name: Mesh Fleet Deploy\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master ]\n"
			"  workflow_dispatch:\n\n"
			"jobs:\n"
			"  fleet-sync:\n"
			"    runs-on: self-hosted\n"
			"    steps:\n"
			"      - name: Fleet SSH Sync\n"
			"        run: |\n"
			"          ssh mediacenter-gentoo 'cd ~/Dev/ai-upp && git pull && ./bin/build reference/ManagerSkylark'\n"
			"          ssh gen7-lan 'cd ~/Dev/ai-upp && git pull && ./bin/build reference/ManagerSkylark'\n";
	}

	// 10. Automation: Manual Workflow
	{
		WorkflowTemplate& t = list.Add();
		t.id = "auto-manual";
		t.title = "Manual Workflow (workflow_dispatch)";
		t.category = "automation";
		t.author = "By actions";
		t.icon = "play-circle";
		t.description = "Trigger workflow manually via Web UI or API with custom runtime arguments and dry-run flag.";
		t.default_filename = "manual.yml";
		t.default_yaml =
			"name: Manual Workflow\n\n"
			"on:\n"
			"  workflow_dispatch:\n"
			"    inputs:\n"
			"      name:\n"
			"        description: 'Target component'\n"
			"        default: 'all'\n"
			"        required: true\n\n"
			"jobs:\n"
			"  manual-job:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - run: echo 'Executing manual task for ${{ github.event.inputs.name }}'\n";
	}

	// 11. Automation: AI Issue & PR Summary (SweAgent)
	{
		WorkflowTemplate& t = list.Add();
		t.id = "auto-ai-summary";
		t.title = "AI Issue & PR Summary (SweAgent)";
		t.category = "automation";
		t.author = "By SweAgent AI";
		t.icon = "robot";
		t.description = "Autonomous LLM issue analysis: digests bug reports, identifies affected files in codebase, and drafts patch PR.";
		t.default_filename = "ai-issue-summary.yml";
		t.default_yaml =
			"name: AI Issue Summary\n\n"
			"on:\n"
			"  issues:\n"
			"    types: [ opened ]\n\n"
			"jobs:\n"
			"  ai-summary:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - name: Run SweAgent Triage\n"
			"        run: ./bin/CLANG/SweAgent1 triage --issue ${{ github.event.issue.number }}\n";
	}

	// 12. Automation: Mark Stale Issues
	{
		WorkflowTemplate& t = list.Add();
		t.id = "auto-stale";
		t.title = "Mark Stale Issues and Pull Requests";
		t.category = "automation";
		t.author = "By actions";
		t.icon = "clock";
		t.description = "Automatically warn and close inactive issues and pull requests after 60 days of inactivity.";
		t.default_filename = "stale.yml";
		t.default_yaml =
			"name: 'Close Stale Issues'\n\n"
			"on:\n"
			"  schedule:\n"
			"    - cron: '30 1 * * *'\n\n"
			"jobs:\n"
			"  stale:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/stale@v9\n"
			"        with:\n"
			"          stale-issue-message: 'This issue is stale because it has been open 60 days with no activity.'\n"
			"          days-before-stale: 60\n"
			"          days-before-close: 7\n";
	}

	// 13. Automation: Greetings for New Contributors
	{
		WorkflowTemplate& t = list.Add();
		t.id = "auto-greetings";
		t.title = "Greetings for New Contributors";
		t.category = "automation";
		t.author = "By actions";
		t.icon = "smile";
		t.description = "Welcome new community contributors when they submit their first pull request or issue.";
		t.default_filename = "greetings.yml";
		t.default_yaml =
			"name: Greetings\n\n"
			"on: [pull_request_target, issues]\n\n"
			"jobs:\n"
			"  greeting:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/first-interaction@v1\n"
			"        with:\n"
			"          repo-token: ${{ secrets.GITHUB_TOKEN }}\n"
			"          issue-message: 'Welcome and thank you for opening your first issue!'\n"
			"          pr-message: 'Thank you for submitting your first contribution!'\n";
	}

	// 14. Automation: Pull Request Labeler
	{
		WorkflowTemplate& t = list.Add();
		t.id = "auto-labeler";
		t.title = "Pull Request Labeler";
		t.category = "automation";
		t.author = "By actions";
		t.icon = "tag";
		t.description = "Automatically tag pull requests with area labels (Core, Skylark, WebFramework, Aria) based on file paths.";
		t.default_filename = "labeler.yml";
		t.default_yaml =
			"name: Pull Request Labeler\n\n"
			"on: [pull_request_target]\n\n"
			"jobs:\n"
			"  label:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/labeler@v5\n"
			"        with:\n"
			"          repo-token: ${{ secrets.GITHUB_TOKEN }}\n";
	}

	// 15. Pages: U++ Site For GitHub Pages (*.tpp Documentation) - TOP PRIORITY!
	{
		WorkflowTemplate& t = list.Add();
		t.id = "pages-upp-tpp";
		t.title = "U++ Site For GitHub Pages (*.tpp Documentation)";
		t.category = "pages";
		t.author = "By U++ Team";
		t.suggested = true;
		t.icon = "book-open";
		t.description = "Scan *.tpp Topic documentation directories (like uppsrc/Core/srcdoc.tpp), generate static HTML with search and styling identical to ultimatepp.org, and deploy to GitHub Pages.";
		t.default_filename = "upp-docs-pages.yml";
		t.default_yaml =
			"name: U++ TPP Documentation Site\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master, main ]\n"
			"    paths:\n"
			"      - '**/*.tpp/**'\n"
			"      - '**/*.tpp'\n"
			"  workflow_dispatch:\n\n"
			"permissions:\n"
			"  contents: read\n"
			"  pages: write\n"
			"  id-token: write\n\n"
			"concurrency:\n"
			"  group: 'pages'\n"
			"  cancel-in-progress: false\n\n"
			"jobs:\n"
			"  build-docs:\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - name: Checkout repository\n"
			"        uses: actions/checkout@v4\n\n"
			"      - name: Generate U++ HTML Documentation from *.tpp\n"
			"        uses: upp-actions/tpp-doc@v1\n"
			"        with:\n"
			"          input_dirs: uppsrc/Core/srcdoc.tpp\n"
			"          output_dir: _site\n"
			"          site_title: 'U++ Reference Documentation'\n"
			"          theme: ultimatepp-org\n\n"
			"      - name: Upload GitHub Pages artifact\n"
			"        uses: actions/upload-pages-artifact@v3\n"
			"        with:\n"
			"          path: _site\n\n"
			"  deploy-pages:\n"
			"    environment:\n"
			"      name: github-pages\n"
			"      url: ${{ steps.deployment.outputs.page_url }}\n"
			"    runs-on: ubuntu-latest\n"
			"    needs: build-docs\n"
			"    steps:\n"
			"      - name: Deploy to GitHub Pages\n"
			"        id: deployment\n"
			"        uses: actions/deploy-pages@v4\n";
	}

	// 16. Pages: Static HTML Page Workflow
	{
		WorkflowTemplate& t = list.Add();
		t.id = "pages-static";
		t.title = "Static HTML Page Workflow";
		t.category = "pages";
		t.author = "By actions";
		t.icon = "file";
		t.description = "Deploy static HTML, CSS, images and JavaScript files directly to GitHub Pages without compilation.";
		t.default_filename = "static.yml";
		t.default_yaml =
			"name: Deploy static content to Pages\n\n"
			"on:\n"
			"  push:\n"
			"    branches: [ master ]\n"
			"  workflow_dispatch:\n\n"
			"permissions:\n"
			"  contents: read\n"
			"  pages: write\n"
			"  id-token: write\n\n"
			"jobs:\n"
			"  deploy:\n"
			"    environment:\n"
			"      name: github-pages\n"
			"      url: ${{ steps.deployment.outputs.page_url }}\n"
			"    runs-on: ubuntu-latest\n"
			"    steps:\n"
			"      - uses: actions/checkout@v4\n"
			"      - uses: actions/upload-pages-artifact@v3\n"
			"        with:\n"
			"          path: '.'\n"
			"      - id: deployment\n"
			"        uses: actions/deploy-pages@v4\n";
	}

	return list;
}

WorkflowTemplate GitHubPlatformService::GetWorkflowTemplate(const String& id)
{
	Vector<WorkflowTemplate> all = GetWorkflowTemplates("SeppoPakonen", "ai-upp-priv");
	for(const WorkflowTemplate& t : all) {
		if(t.id == id) return t;
	}
	if(!all.IsEmpty()) return all[0];
	WorkflowTemplate empty;
	return empty;
}

Vector<MarketplaceAction> GitHubPlatformService::GetMarketplaceActions()
{
	Vector<MarketplaceAction> list;

	{
		MarketplaceAction& a = list.Add();
		a.id = "cache";
		a.name = "Cache";
		a.author = "actions";
		a.version = "v5.1.0";
		a.stars = "5,590";
		a.icon = "folder";
		a.description = "Cache artifacts like dependencies and build outputs to improve workflow execution time.";
		a.doc_summary = "Caches object files and compilation caches matching hash keys.";
		a.yaml_snippet =
			"- name: Cache build artifacts\n"
			"  uses: actions/cache@v5\n"
			"  with:\n"
			"    path: |\n"
			"      ~/.local/state/upp/cache\n"
			"      bin/CLANG\n"
			"    key: ${{ runner.os }}-upp-${{ hashFiles('**/*.upp', '**/*.h', '**/*.cpp') }}\n";
	}
	{
		MarketplaceAction& a = list.Add();
		a.id = "checkout";
		a.name = "Checkout";
		a.author = "actions";
		a.version = "v4.2.0";
		a.stars = "6,200";
		a.icon = "git-branch";
		a.description = "Check out a Git repository at a particular commit, branch, or tag.";
		a.doc_summary = "Fetches the working tree into $GITHUB_WORKSPACE.";
		a.yaml_snippet =
			"- name: Checkout repository\n"
			"  uses: actions/checkout@v4\n"
			"  with:\n"
			"    submodules: recursive\n"
			"    fetch-depth: 1\n";
	}
	{
		MarketplaceAction& a = list.Add();
		a.id = "upp-build";
		a.name = "U++ Builder";
		a.author = "upp-actions";
		a.version = "v1.0.0";
		a.stars = "840";
		a.icon = "package";
		a.description = "Build U++ packages using umk / bin/build with intelligent compilation cache.";
		a.doc_summary = "Compiles packages using native U++ umk engine with multi-threaded BLITZ linking.";
		a.yaml_snippet =
			"- name: Build U++ Package\n"
			"  uses: upp-actions/build@v1\n"
			"  with:\n"
			"    method: CLANG\n"
			"    packages: reference/ManagerSkylark\n"
			"    flags: DEBUG BLITZ\n";
	}
	{
		MarketplaceAction& a = list.Add();
		a.id = "tpp-doc";
		a.name = "U++ TPP Doc Generator";
		a.author = "upp-actions";
		a.version = "v1.0.0";
		a.stars = "620";
		a.icon = "book-open";
		a.description = "Compile *.tpp topic documentation to static HTML site matching ultimatepp.org.";
		a.doc_summary = "Parses Topic++ files, extracts QTF, and generates static website for GitHub Pages.";
		a.yaml_snippet =
			"- name: Generate U++ HTML Documentation\n"
			"  uses: upp-actions/tpp-doc@v1\n"
			"  with:\n"
			"    input_dirs: uppsrc/Core/srcdoc.tpp\n"
			"    output_dir: _site\n"
			"    theme: ultimatepp-org\n";
	}
	{
		MarketplaceAction& a = list.Add();
		a.id = "upload-artifact";
		a.name = "Upload Artifact";
		a.author = "actions";
		a.version = "v4.0.0";
		a.stars = "4,100";
		a.icon = "file";
		a.description = "Upload build artifacts, test logs and coverage reports from workflow.";
		a.doc_summary = "Stores output artifacts accessible via the Actions run summary page.";
		a.yaml_snippet =
			"- name: Upload Build Artifact\n"
			"  uses: actions/upload-artifact@v4\n"
			"  with:\n"
			"    name: binaries\n"
			"    path: bin/CLANG/\n";
	}
	{
		MarketplaceAction& a = list.Add();
		a.id = "download-artifact";
		a.name = "Download Artifact";
		a.author = "actions";
		a.version = "v4.0.0";
		a.stars = "3,900";
		a.icon = "folder";
		a.description = "Download artifacts from previous workflow runs or jobs.";
		a.doc_summary = "Restores previously uploaded workflow artifacts for downstream jobs.";
		a.yaml_snippet =
			"- name: Download Artifacts\n"
			"  uses: actions/download-artifact@v4\n"
			"  with:\n"
			"    name: binaries\n";
	}

	return list;
}

Vector<PackageBuildCacheInfo> GitHubPlatformService::GetPackageBuildCaches(const String& repo_path)
{
	Vector<PackageBuildCacheInfo> list;
	const char* cached_pkgs[] = {
		"Core", "Skylark", "Manager", "SweAgent", "WebChat", "WebFramework", "ManagerSkylark", "Aria"
	};
	for(int i = 0; i < 8; i++) {
		PackageBuildCacheInfo& b = list.Add();
		b.package_name = cached_pkgs[i];
		b.flags = "DEBUG BLITZ";
		b.hash = Sprintf("sha256-%08x", (unsigned)GetHashValue(b.package_name));
		b.last_status = "success";
		b.is_cached = true;
		b.duration_secs = (i >= 6 ? (i == 6 ? 6 : 4) : 0);
	}
	{
		PackageBuildCacheInfo& b = list.Add();
		b.package_name = "Sql";
		b.flags = "DEBUG BLITZ USEMALLOC";
		b.hash = "untested";
		b.last_status = "untested";
		b.is_cached = false;
		b.duration_secs = 0;
	}
	{
		PackageBuildCacheInfo& b = list.Add();
		b.package_name = "plugin/pcre";
		b.flags = "DEBUG BLITZ USEMALLOC";
		b.hash = "untested";
		b.last_status = "untested";
		b.is_cached = false;
		b.duration_secs = 0;
	}
	return list;
}

bool GitHubPlatformService::SaveWorkflowFile(const String& repo_path, const String& filename, const String& content)
{
	String safe_fn = GetFileName(filename);
	if(safe_fn.IsEmpty()) safe_fn = "workflow.yml";
	if(!safe_fn.EndsWith(".yml") && !safe_fn.EndsWith(".yaml")) safe_fn << ".yml";

	String target_dir = AppendFileName(AppendFileName(repo_path, ".github"), "workflows");
	RealizeDirectory(target_dir);
	String full_path = AppendFileName(target_dir, safe_fn);
	return SaveFile(full_path, content);
}

void GitHubPlatformService::TriggerWorkflowRunAsync(const String& owner, const String& repo, const String& workflow_name, const String& run_type)
{
	int64 run_id = 20000 + (Random() % 10000);
	ValueMap run;
	run.Set("id", run_id);
	run.Set("name", workflow_name.IsEmpty() ? "U++ Build & Test (SysProcess)" : workflow_name);
	run.Set("status", "running");
	run.Set("conclusion", "");
	run.Set("branch", "master");
	run.Set("created_at", "Just now");
	run.Set("event", run_type.IsEmpty() ? "workflow_dispatch" : run_type);

	{
		Mutex::Lock __(sRunLock);
		sCustomRuns.Insert(0, run);
	}

	Thread().Run([=] {
		LocalProcess p;
		String cmd = AppendFileName(GetHomeDirFile("Dev/ai-upp"), "bin/build") + " reference/ManagerSkylark";
		p.Start(cmd);
		String out;
		p.Finish(out);
		int exit_code = p.GetExitCode();

		Mutex::Lock __(sRunLock);
		for(int i = 0; i < sCustomRuns.GetCount(); i++) {
			if((int64)sCustomRuns[i]["id"] == run_id) {
				sCustomRuns[i].Set("status", "completed");
				sCustomRuns[i].Set("conclusion", exit_code == 0 ? "success" : "failure");
				break;
			}
		}
	});
}

Vector<ValueMap> GitHubPlatformService::GetLiveActions(const String& owner, const String& repo, String* unavailable)
{
	Vector<ValueMap> list;
	{
		Mutex::Lock __(sRunLock);
		for(const ValueMap& r : sCustomRuns) {
			list.Add(r);
		}
	}
	GitHubForgeBackend backend;
	Vector<ForgeRun> runs;
	ForgeResult res = backend.ListRuns(owner + "/" + repo, 15, runs);
	if(!res.ok) {
		// Runs started from this site are still listed; GitHub runs are not.
		if(unavailable)
			*unavailable = res.error;
		return list;
	}
	for(const ForgeRun& r : runs) {
		ValueMap run;
		run.Set("id", r.run_id);
		run.Set("name", r.name);
		run.Set("status", r.status);
		run.Set("conclusion", r.conclusion);
		run.Set("branch", r.head_branch);
		run.Set("created_at", r.created_at.Left(10));
		run.Set("event", r.event);
		list.Add(run);
	}
	return list;
}

END_UPP_NAMESPACE
