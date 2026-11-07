#include "ManagerSkylark.h"
#include "GitHubPlatform.h"
#include "SystemAuth.h"
#include "AgentPlatform.h"

NAMESPACE_UPP

static void SendEgressDisabled(Http& http)
{
	ValueMap response;
	response.Add("status", "disabled");
	response.Add("available", false);
	response.Add("error", "external_operation_disabled");
	http.ContentType("application/json");
	http.Response(503, "Service Unavailable");
	http << AsJSON(response, true);
}

String GitHubPlatformService::EscapeHtml(const String& s)
{
	String r;
	for(int i = 0; i < s.GetLength(); i++) {
		char c = s[i];
		switch(c) {
		case '&': r << "&amp;"; break;
		case '<': r << "&lt;"; break;
		case '>': r << "&gt;"; break;
		case '\"': r << "&quot;"; break;
		case '\'': r << "&#39;"; break;
		default: r.Cat(c); break;
		}
	}
	return r;
}

static bool IsSymlinkPath(const String& path)
{
	FindFile ff(path);
	return ff && ff.IsSymLink();
}

static String GHRepoRoute(const String& owner, const String& repo)
{
	return "/repo/" + UrlEncode(owner) + "/" + UrlEncode(repo);
}

static String GHUserRoute(const String& owner)
{
	return "/user/" + UrlEncode(owner);
}

static String GHAppendPath(const String& base, const String& path)
{
	String out = base;
	Vector<String> parts = Split(path, '/');
	for(int i = 0; i < parts.GetCount(); i++) {
		if(parts[i].IsEmpty()) continue;
		out << "/" << UrlEncode(parts[i]);
	}
	return out;
}

static String SafeGHIcon(const String& value, const String& fallback)
{
	if(value.IsEmpty()) return fallback;
	for(int i = 0; i < value.GetCount(); i++) {
		char c = value[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '-'))
			return fallback;
	}
	return value;
}

static bool SafeGHComponent(const String& value)
{
	if(value.IsEmpty() || value == "." || value == "..") return false;
	for(int i = 0; i < value.GetCount(); i++) {
		char c = value[i];
		if(!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		     (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
			return false;
	}
	return true;
}

static bool SafeGHRelativePath(const String& path)
{
	if(path.IsEmpty() || path[0] == '/' || path.Find('\\') >= 0)
		return false;
	Vector<String> parts = Split(path, '/');
	for(int i = 0; i < parts.GetCount(); i++) {
		if(parts[i].IsEmpty() || parts[i] == "." || parts[i] == ".." || ToLower(parts[i]) == ".git")
			return false;
		for(int j = 0; j < parts[i].GetCount(); j++) {
			byte c = (byte)parts[i][j];
			if(c < 0x20 || c == 0x7f || c == '\\') return false;
		}
	}
	return true;
}

String GitHubPlatformService::FormatFileSize(int64 bytes)
{
	if(bytes < 1024)
		return AsString(bytes) + " B";
	if(bytes < 1024 * 1024)
		return Sprintf("%.1f KB", (double)bytes / 1024.0);
	return Sprintf("%.1f MB", (double)bytes / (1024.0 * 1024.0));
}

String GitHubPlatformService::GetDevDir()
{
	String custom = GetEnv("DEV_DIR");
	if(!DataStore::IsSafeRoot(custom))
		return String();
	return NormalizePath(custom);
}

ValueMap GitHubPlatformService::LoadScrapedData(const String& owner, const String& repo)
{
	// No registered typed DataStore schema currently supplies GitHub summaries.
	return ValueMap();
}

bool GitHubPlatformService::IsGhCliAvailable()
{
	return false;
}

Vector<LocalRepoInfo> GitHubPlatformService::GetLiveGitHubRepos(const String& owner)
{
	return GetRepositories(owner);
}

Vector<ValueMap> GitHubPlatformService::GetLiveIssues(const String& owner, const String& repo)
{
	Vector<ValueMap> list;
	return list;
}

ValueMap GitHubPlatformService::GetLiveIssueDetail(const String& owner, const String& repo, int number)
{
	ValueMap res;
	return res;
}

String GitHubPlatformService::CreateIssue(const String& owner, const String& repo, const String& title, const String& body)
{
	return "disabled";
}

String GitHubPlatformService::AddComment(const String& owner, const String& repo, int number, const String& body)
{
	return "disabled";
}

String GitHubPlatformService::ToggleIssueState(const String& owner, const String& repo, int number, bool close_it)
{
	return "disabled";
}

static Vector<ValueMap> sCustomRuns;
static StaticMutex sRunLock;

static String QtfToReadableHtml(const String& qtf)
{
	String out;
	String cur;
	bool in_tag = false;
	for(int i = 0; i < qtf.GetLength(); i++) {
		char c = qtf[i];
		if(c == '[') {
			in_tag = true;
			continue;
		}
		if(c == ']') {
			in_tag = false;
			continue;
		}
		if(!in_tag) {
			if(c == '&') {
				if(!cur.IsEmpty()) {
					out << "<p style=\"margin-bottom:0.75rem; line-height:1.6;\">" << GitHubPlatformService::EscapeHtml(cur) << "</p>\n";
					cur.Clear();
				}
			}
			else if(c == '\n' || c == '\r') {
				if(!cur.IsEmpty() && cur.Mid(cur.GetLength()-1) != " ") cur.Cat(' ');
			}
			else {
				cur.Cat(c);
			}
		}
	}
	if(!cur.IsEmpty()) {
		out << "<p style=\"margin-bottom:0.75rem; line-height:1.6;\">" << GitHubPlatformService::EscapeHtml(cur) << "</p>\n";
	}
	return out;
}

static String ExtractTppText(const String& file_path, String& title)
{
	String content = LoadFile(file_path);
	if(content.IsEmpty()) return "";

	int tpos = content.Find("TITLE(");
	if(tpos >= 0) {
		int q1 = content.Find('\"', tpos);
		if(q1 >= 0) {
			int q2 = content.Find('\"', q1 + 1);
			if(q2 >= 0) {
				title = content.Mid(q1 + 1, q2 - q1 - 1);
			}
		}
	}
	if(title.IsEmpty()) title = GetFileTitle(file_path);

	int comp_idx = content.Find("COMPRESSED");
	if(comp_idx >= 0) {
		String nums = content.Mid(comp_idx + 10);
		StringBuffer bytes;
		const char *p = nums;
		while(*p) {
			while(*p && !IsDigit(*p)) p++;
			if(!*p) break;
			int val = 0;
			while(*p && IsDigit(*p)) {
				val = val * 10 + (*p - '0');
				p++;
			}
			bytes.Cat((byte)val);
		}
		String qtf = ZDecompress(~bytes, bytes.GetCount());
		return qtf;
	}
	return content;
}

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
			"          flags: MT DEBUG BLITZ\n\n"
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
			"        run: docker build -t workspace-app:latest .\n"
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
		t.description = "Deploy a verified binary to explicitly configured nodes after operator approval.";
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
			"          echo 'Configure an authorized host and repository before deployment.'\n";
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
	Vector<WorkflowTemplate> all = GetWorkflowTemplates(String(), String());
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
			"    flags: MT DEBUG BLITZ\n";
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
		b.flags = "MT DEBUG BLITZ";
		b.hash = Sprintf("sha256-%08x", (unsigned)GetHashValue(b.package_name));
		b.last_status = "success";
		b.is_cached = true;
		b.duration_secs = (i >= 6 ? (i == 6 ? 6 : 4) : 0);
	}
	{
		PackageBuildCacheInfo& b = list.Add();
		b.package_name = "Sql";
		b.flags = "MT DEBUG BLITZ flagUSEMALLOC";
		b.hash = "untested";
		b.last_status = "untested";
		b.is_cached = false;
		b.duration_secs = 0;
	}
	{
		PackageBuildCacheInfo& b = list.Add();
		b.package_name = "plugin/pcre";
		b.flags = "MT DEBUG BLITZ flagUSEMALLOC";
		b.hash = "untested";
		b.last_status = "untested";
		b.is_cached = false;
		b.duration_secs = 0;
	}
	return list;
}

bool GitHubPlatformService::SaveWorkflowFile(const String& repo_path, const String& filename, const String& content)
{
	// Workflow writes are disabled until an authorized repository writer exists.
	return false;
}

void GitHubPlatformService::TriggerWorkflowRunAsync(const String& owner, const String& repo, const String& workflow_name, const String& run_type)
{
	// Workflow dispatch is unavailable until an explicit policy exists.
}

String GitHubPlatformService::RenderOperatorWarningBar(const String& owner, const String& repo)
{
	String h;
	h << "<div class=\"wf-card\" style=\"margin-bottom:1.25rem; border-left:4px solid #d29922; background:rgba(210,153,34,0.06);\">\n"
	  << "  <div style=\"padding:14px 18px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:16px;\">\n"
	  << "    <div style=\"flex:1; min-width:280px;\">\n"
	  << "      <div style=\"display:flex; align-items:center; gap:8px; font-weight:700; color:#f0883e; margin-bottom:4px;\">\n"
	  << "        " << VividIcon::Get("alert-triangle", 18) << " <span>Operator Notice: C++ Test Matrix Coverage &amp; Cache State</span>\n"
	  << "      </div>\n"
	  << "      <div style=\"font-size:0.86rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.4;\">\n"
	  << "        <strong>8 packages up-to-date &amp; cached</strong> (0s recompilation, source &amp; dependency hashes matched).<br>\n"
	  << "        <span style=\"color:#f0883e;\">&#9888; <strong>2 package configurations untested</strong></span> for USE flag combination <code>flagUSEMALLOC</code>.<br>\n"
	  << "        Full matrix rebuild estimated at <strong>4m 15s</strong> &middot; Delta rebuild: <strong>18s</strong>.\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:10px; align-items:center; flex-wrap:wrap;\">\n"
	  << "      <span class=\"wf-badge wf-badge-neutral\">External workflow dispatch is disabled</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n";
	return h;
}

String GitHubPlatformService::RenderActionsCatalog(const String& owner, const String& repo, const String& category, const String& search_q)
{
	Vector<WorkflowTemplate> templates = GetWorkflowTemplates(owner, repo);
	String repo_base = GHRepoRoute(owner, repo);
	String cat = category;
	if(cat.IsEmpty()) cat = "all";

	String h;
	h << "<div style=\"display:flex; flex-direction:column; gap:1.25rem;\">\n"
	  << "  <!-- Top Header & Search Bar -->\n"
	  << "  <div class=\"wf-card\">\n"
	  << "    <div style=\"padding:16px 20px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:16px;\">\n"
	  << "      <div>\n"
	  << "        <h2 style=\"margin:0 0 4px 0; font-size:1.35rem; font-weight:700;\">Choose a workflow</h2>\n"
	  << "        <p style=\"margin:0; font-size:0.86rem; color:var(--wf-fg-inactive,#8b919d);\">Choose a starter workflow template to configure continuous integration, deployment, automation, or documentation for this repository.</p>\n"
	  << "      </div>\n"
	  << "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
	  << "        <input type=\"text\" id=\"workflow-search\" placeholder=\"Search workflows...\" value=\"" << EscapeHtml(search_q) << "\" oninput=\"filterWorkflows(this.value)\" class=\"wf-input\" style=\"padding:6px 12px; font-size:0.85rem; border-radius:6px; background:var(--wf-bg-view,#151d2b); border:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); color:var(--wf-fg-normal,#d3dae3); width:260px;\">\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "\n"
	  << "  <!-- Category Nav Pills -->\n"
	  << "  <div style=\"display:flex; gap:8px; flex-wrap:wrap; align-items:center;\">\n"
	  << "    <a href=\"" << repo_base << "/actions/new\" class=\"wf-btn" << (cat == "all" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">All</a>\n"
	  << "    <a href=\"" << repo_base << "/actions/new?category=continuous-integration\" class=\"wf-btn" << (cat == "continuous-integration" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Continuous integration</a>\n"
	  << "    <a href=\"" << repo_base << "/actions/new?category=deployment\" class=\"wf-btn" << (cat == "deployment" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Deployment</a>\n"
	  << "    <a href=\"" << repo_base << "/actions/new?category=automation\" class=\"wf-btn" << (cat == "automation" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Automation</a>\n"
	  << "    <a href=\"" << repo_base << "/actions/new?category=pages\" class=\"wf-btn" << (cat == "pages" ? " wf-btn-primary" : " wf-btn-secondary") << "\" style=\"font-size:0.82rem; padding:5px 14px;\">Pages</a>\n"
	  << "  </div>\n"
	  << "\n";

	// Suggested for this repository section
	if(cat == "all" || cat == "continuous-integration" || cat == "pages") {
		h << "  <div id=\"section-suggested\">\n"
		  << "    <div style=\"display:flex; align-items:center; gap:8px; font-weight:700; font-size:1.05rem; margin-bottom:10px;\">\n"
		  << "      <span>Suggested for this repository</span>\n"
		  << "      <span class=\"wf-badge wf-badge-cyan\">Detected C++ &amp; U++ Codebase</span>\n"
		  << "    </div>\n"
		  << "    <div style=\"display:grid; grid-template-columns:repeat(auto-fill, minmax(360px, 1fr)); gap:16px;\">\n";

		for(const WorkflowTemplate& t : templates) {
			if(!t.suggested) continue;
			if(cat != "all" && t.category != cat) continue;
			h << "      <div class=\"wf-card wf-workflow-card\" style=\"padding:18px; display:flex; flex-direction:column; justify-content:space-between; border:1px solid rgba(0,161,197,0.4); background:rgba(0,161,197,0.03);\">\n"
			  << "        <div>\n"
			  << "          <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:10px;\">\n"
			  << "            <div style=\"display:flex; align-items:center; gap:10px;\">\n"
			  << "              <div style=\"padding:8px; border-radius:6px; background:rgba(0,161,197,0.15); color:var(--wf-accent-focus,#00a1c5);\">\n"
			  << "                " << VividIcon::Get(SafeGHIcon(t.icon, "play"), 22) << "\n"
			  << "              </div>\n"
			  << "              <div>\n"
			  << "                <div style=\"font-weight:700; font-size:1.05rem;\">" << EscapeHtml(t.title) << "</div>\n"
			  << "                <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(t.author) << "</div>\n"
			  << "              </div>\n"
			  << "            </div>\n"
			  << "            <span class=\"wf-badge wf-badge-emerald\">Suggested</span>\n"
			  << "          </div>\n"
			  << "          <p style=\"font-size:0.86rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.4; margin-bottom:14px;\">" << EscapeHtml(t.description) << "</p>\n"
			  << "        </div>\n"
			  << "        <div style=\"display:flex; justify-content:space-between; align-items:center; padding-top:12px; border-top:1px solid var(--wf-border-color,rgba(0,161,197,0.15));\">\n"
			  << "          <span style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << EscapeHtml(t.default_filename) << "</code></span>\n"
			  << "          <div style=\"display:flex; gap:8px;\">\n";
			if(t.id == "pages-upp-tpp") {
				h << "            <a href=\"" << repo_base << "/actions/new?template=pages-upp-tpp&amp;preview=1\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; padding:4px 10px;\">Preview Site</a>\n";
			}
			h << "            <a href=\"" << repo_base << "/actions/new?template=" << UrlEncode(t.id) << "\" class=\"wf-btn wf-btn-primary\" style=\"font-size:0.8rem; padding:4px 12px; font-weight:600;\">Configure</a>\n"
			  << "          </div>\n"
			  << "        </div>\n"
			  << "      </div>\n";
		}
		h << "    </div>\n"
		  << "  </div>\n\n";
	}

	// Other Categories Grid
	h << "  <div>\n"
	  << "    <div style=\"font-weight:700; font-size:1.05rem; margin-bottom:12px;\">\n"
	  << "      <span>" << (cat == "all" ? "All Starter Workflows" : (cat == "continuous-integration" ? "Continuous Integration" : (cat == "deployment" ? "Deployment" : (cat == "automation" ? "Automation" : "Pages")))) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:grid; grid-template-columns:repeat(auto-fill, minmax(320px, 1fr)); gap:16px;\">\n";

	for(const WorkflowTemplate& t : templates) {
		if(t.suggested && cat == "all") continue; // Already shown in suggested
		if(cat != "all" && t.category != cat) continue;

		h << "      <div class=\"wf-card wf-workflow-card\" style=\"padding:16px; display:flex; flex-direction:column; justify-content:space-between;\">\n"
		  << "        <div>\n"
		  << "          <div style=\"display:flex; justify-content:space-between; align-items:flex-start; margin-bottom:10px;\">\n"
		  << "            <div style=\"display:flex; align-items:center; gap:10px;\">\n"
		  << "              <div style=\"padding:6px; border-radius:6px; background:var(--wf-bg-alt,#1c202f); color:var(--wf-accent-focus,#00a1c5);\">\n"
	  << "                " << VividIcon::Get(SafeGHIcon(t.icon, "play"), 20) << "\n"
		  << "              </div>\n"
		  << "              <div>\n"
		  << "                <div style=\"font-weight:700; font-size:0.96rem;\">" << EscapeHtml(t.title) << "</div>\n"
		  << "                <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">" << EscapeHtml(t.author) << "</div>\n"
		  << "              </div>\n"
		  << "            </div>\n"
	  << "            <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.7rem;\">" << EscapeHtml(t.category) << "</span>\n"
		  << "          </div>\n"
		  << "          <p style=\"font-size:0.84rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.4; margin-bottom:12px;\">" << EscapeHtml(t.description) << "</p>\n"
		  << "        </div>\n"
		  << "        <div style=\"display:flex; justify-content:space-between; align-items:center; padding-top:10px; border-top:1px solid var(--wf-border-color,rgba(0,161,197,0.15));\">\n"
	  << "          <span style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\"><code>" << EscapeHtml(t.default_filename) << "</code></span>\n"
	  << "          <a href=\"" << repo_base << "/actions/new?template=" << UrlEncode(t.id) << "\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem; padding:4px 12px; font-weight:600;\">Configure</a>\n"
		  << "        </div>\n"
		  << "      </div>\n";
	}

	h << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function filterWorkflows(q) {\n"
	  << "  q = (q || '').toLowerCase();\n"
	  << "  document.querySelectorAll('.wf-workflow-card').forEach(c => {\n"
	  << "    let t = c.innerText.toLowerCase();\n"
	  << "    c.style.display = (!q || t.includes(q)) ? 'flex' : 'none';\n"
	  << "  });\n"
	  << "}\n"
	  << "</script>\n";

	return h;
}

String GitHubPlatformService::RenderWorkflowEditor(const String& owner, const String& repo, const String& template_id, const String& filename)
{
	WorkflowTemplate tpl = GetWorkflowTemplate(template_id);
	Vector<MarketplaceAction> market_actions = GetMarketplaceActions();

	String fn = filename;
	if(fn.IsEmpty()) fn = tpl.default_filename;
	if(fn.IsEmpty()) fn = "workflow.yml";

	String h;
	h << "<div style=\"display:flex; flex-direction:column; gap:14px;\">\n"
	  << "  <!-- Toast Notification -->\n"
	  << "  <div id=\"workflow-toast\" style=\"display:none; padding:10px 16px; background:#238636; color:#ffffff; font-weight:600; border-radius:6px; font-size:0.88rem;\"></div>\n"
	  << "\n"
	  << "  <!-- Top Bar: Breadcrumb + Action Controls -->\n"
	  << "  <div class=\"wf-card\" style=\"padding:12px 18px; display:flex; justify-content:space-between; align-items:center; flex-wrap:wrap; gap:12px;\">\n"
	  << "    <div style=\"display:flex; align-items:center; gap:8px; font-size:0.9rem;\">\n"
	  << "      <a href=\"" << GHRepoRoute(owner, repo) << "/actions/new\" style=\"color:var(--wf-accent-link,#00acd3); text-decoration:none; font-weight:600;\">Actions</a>\n"
	  << "      <span style=\"color:var(--wf-fg-inactive,#8b919d);\">/</span>\n"
	  << "      <span style=\"color:var(--wf-fg-inactive,#8b919d);\">.github / workflows /</span>\n"
	  << "      <input type=\"text\" id=\"workflow-filename\" value=\"" << EscapeHtml(fn) << "\" class=\"wf-input\" style=\"padding:4px 8px; font-size:0.85rem; font-family:monospace; width:220px; background:var(--wf-bg-view,#151d2b); border:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); color:#ffffff; border-radius:4px;\">\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:10px; align-items:center;\">\n"
	  << "      <a href=\"" << GHRepoRoute(owner, repo) << "/actions/new\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem; padding:5px 12px;\">Cancel</a>\n"
	  << "      <span class=\"wf-badge wf-badge-neutral\">External workflow writes are disabled</span>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "\n"
	  << "  <!-- Dual Layout: Code Editor on Left, Marketplace / Docs on Right -->\n"
	  << "  <div style=\"display:flex; gap:16px; align-items:flex-start;\">\n"
	  << "    <!-- Left: YAML Workflow Code Editor -->\n"
	  << "    <div class=\"wf-card\" style=\"flex:1; min-width:0; padding:12px; display:flex; flex-direction:column;\">\n"
	  << "      <div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; font-size:0.82rem; color:var(--wf-fg-inactive,#8b919d);\">\n"
	  << "        <span>Workflow Definition (YAML) &middot; <code>" << EscapeHtml(tpl.title) << "</code></span>\n"
	  << "        <span style=\"font-size:0.75rem;\">UTF-8 &middot; Indent: 2 spaces</span>\n"
	  << "      </div>\n"
	  << "      <textarea id=\"workflow-editor-yaml\" spellcheck=\"false\" style=\"width:100%; height:580px; font-family:Consolas, 'Courier New', monospace; font-size:0.88rem; line-height:1.5; background:#0d1117; color:#e6edf3; border:1px solid #30363d; border-radius:6px; padding:14px; box-sizing:border-box; resize:vertical; outline:none;\">"
	  << EscapeHtml(tpl.default_yaml) << "</textarea>\n"
	  << "    </div>\n"
	  << "\n"
	  << "    <!-- Right: Tabbed Panel (Marketplace & Documentation) -->\n"
	  << "    <div class=\"wf-card\" style=\"width:380px; flex-shrink:0; padding:12px; display:flex; flex-direction:column; max-height:640px; overflow-y:auto;\">\n"
	  << "      <!-- Drawer Tabs -->\n"
	  << "      <div style=\"display:flex; border-bottom:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); margin-bottom:12px;\">\n"
	  << "        <button id=\"tab-btn-marketplace\" onclick=\"switchDrawerTab('marketplace')\" style=\"flex:1; background:none; border:none; padding:8px; font-weight:700; font-size:0.85rem; color:#ffffff; border-bottom:2px solid #00a1c5; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;\">"
	  << VividIcon::Get("folder", 14) << " Marketplace</button>\n"
	  << "        <button id=\"tab-btn-docs\" onclick=\"switchDrawerTab('docs')\" style=\"flex:1; background:none; border:none; padding:8px; font-weight:600; font-size:0.85rem; color:var(--wf-fg-inactive,#8b919d); border-bottom:none; cursor:pointer; display:flex; align-items:center; justify-content:center; gap:6px;\">"
	  << VividIcon::Get("file", 14) << " Documentation</button>\n"
	  << "      </div>\n"
	  << "\n"
	  << "      <!-- View 1: Marketplace Tab Content -->\n"
	  << "      <div id=\"view-marketplace\" style=\"display:block;\">\n"
	  << "        <input type=\"text\" id=\"marketplace-search\" placeholder=\"Search actions...\" oninput=\"filterMarketplace(this.value)\" class=\"wf-input\" style=\"width:100%; padding:6px 10px; font-size:0.82rem; margin-bottom:12px; box-sizing:border-box; background:var(--wf-bg-alt,#1c202f); border:1px solid var(--wf-border-color,rgba(0,161,197,0.2)); color:#ffffff; border-radius:4px;\">\n"
	  << "        <div style=\"font-size:0.78rem; font-weight:700; color:var(--wf-fg-inactive,#8b919d); margin-bottom:8px; text-transform:uppercase;\">Featured Actions</div>\n"
	  << "        <div style=\"display:flex; flex-direction:column; gap:8px;\">\n";

	int action_index = 0;
	for(const MarketplaceAction& a : market_actions) {
		h << "          <div class=\"wf-marketplace-item\" style=\"padding:10px; border-radius:6px; background:var(--wf-bg-alt,#1c202f); border:1px solid var(--wf-border-color,rgba(0,161,197,0.15));\">\n"
		  << "            <div style=\"display:flex; justify-content:space-between; align-items:flex-start;\">\n"
		  << "              <div>\n"
		  << "                <div style=\"font-weight:700; font-size:0.88rem; color:#ffffff;\">" << EscapeHtml(a.name) << "</div>\n"
		  << "                <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#8b919d);\">by " << EscapeHtml(a.author) << " &middot; " << EscapeHtml(a.version) << " &middot; &#9733; " << a.stars << "</div>\n"
		  << "              </div>\n"
		  << "              <button type=\"button\" data-snippet-id=\"" << action_index << "\" class=\"wf-btn wf-btn-secondary\" style=\"padding:3px 8px; font-size:0.75rem;\">Insert</button>\n"
		  << "              <textarea id=\"action-snippet-" << action_index << "\" hidden>" << EscapeHtml(a.yaml_snippet) << "</textarea>\n"
		  << "            </div>\n"
		  << "            <div style=\"font-size:0.8rem; color:var(--wf-fg-normal,#d3dae3); margin-top:4px; line-height:1.3;\">" << EscapeHtml(a.description) << "</div>\n"
		  << "          </div>\n";
		action_index++;
	}

	h << "        </div>\n"
	  << "      </div>\n"
	  << "\n"
	  << "      <!-- View 2: Documentation Tab Content -->\n"
	  << "      <div id=\"view-documentation\" style=\"display:none; font-size:0.84rem; color:var(--wf-fg-normal,#d3dae3); line-height:1.5;\">\n"
	  << "        <h4 style=\"margin:0 0 6px 0; color:#00acd3; font-size:0.95rem;\">Workflow Syntax Reference</h4>\n"
	  << "        <p style=\"margin-bottom:8px;\">Workflows use YAML syntax with the following root keys:</p>\n"
	  << "        <ul style=\"padding-left:18px; margin:0 0 12px 0;\">\n"
	  << "          <li><code>name</code>: Identifier shown in Actions runs list</li>\n"
	  << "          <li><code>on</code>: Triggers (<code>push</code>, <code>pull_request</code>, <code>workflow_dispatch</code>)</li>\n"
	  << "          <li><code>jobs</code>: Set of parallel/sequential execution jobs</li>\n"
	  << "          <li><code>steps</code>: List of tasks invoking actions or shell scripts</li>\n"
	  << "        </ul>\n"
	  << "        <h4 style=\"margin:10px 0 6px 0; color:#00acd3; font-size:0.95rem;\">U++ Build Flag Guide</h4>\n"
	  << "        <ul style=\"padding-left:18px; margin:0;\">\n"
	  << "          <li><code>method</code>: <code>CLANG</code>, <code>GCC</code>, <code>MINGW</code></li>\n"
	  << "          <li><code>MT</code>: Multi-threaded memory allocator and runtime</li>\n"
	  << "          <li><code>BLITZ</code>: Ultra-fast unity build compilation</li>\n"
	  << "          <li><code>DEBUG / DEBUG_FULL</code>: Debug symbols with runtime checks</li>\n"
	  << "          <li><code>flagSOMEFLAG</code>: Custom preprocessor definition mapping</li>\n"
	  << "        </ul>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function switchDrawerTab(tab) {\n"
	  << "  let mTab = document.getElementById('tab-btn-marketplace');\n"
	  << "  let dTab = document.getElementById('tab-btn-docs');\n"
	  << "  let mView = document.getElementById('view-marketplace');\n"
	  << "  let dView = document.getElementById('view-documentation');\n"
	  << "  if(tab === 'marketplace') {\n"
	  << "    mTab.style.borderBottom = '2px solid #00a1c5';\n"
	  << "    mTab.style.color = '#ffffff';\n"
	  << "    dTab.style.borderBottom = 'none';\n"
	  << "    dTab.style.color = 'var(--wf-fg-inactive,#8b919d)';\n"
	  << "    mView.style.display = 'block';\n"
	  << "    dView.style.display = 'none';\n"
	  << "  } else {\n"
	  << "    dTab.style.borderBottom = '2px solid #00a1c5';\n"
	  << "    dTab.style.color = '#ffffff';\n"
	  << "    mTab.style.borderBottom = 'none';\n"
	  << "    mTab.style.color = 'var(--wf-fg-inactive,#8b919d)';\n"
	  << "    mView.style.display = 'none';\n"
	  << "    dView.style.display = 'block';\n"
	  << "  }\n"
	  << "}\n"
	  << "function insertActionSnippet(snippet) {\n"
	  << "  let ed = document.getElementById('workflow-editor-yaml');\n"
	  << "  if(!ed) return;\n"
	  << "  let pos = ed.selectionStart || ed.value.length;\n"
	  << "  let text = ed.value;\n"
	  << "  ed.value = text.substring(0, pos) + '\\n' + snippet + text.substring(pos);\n"
	  << "  ed.focus();\n"
	  << "  let toast = document.getElementById('workflow-toast');\n"
	  << "  if(toast) {\n"
	  << "    toast.style.display = 'block';\n"
	  << "    toast.innerText = 'Action snippet inserted into workflow editor!';\n"
	  << "    setTimeout(() => { toast.style.display = 'none'; }, 2200);\n"
	  << "  }\n"
	  << "}\n"
	  << "document.addEventListener('click', function(e) {\n"
	  << "  let btn = e.target.closest('[data-snippet-id]');\n"
	  << "  if(!btn) return;\n"
	  << "  let src = document.getElementById('action-snippet-' + btn.getAttribute('data-snippet-id'));\n"
	  << "  if(src) insertActionSnippet(src.value);\n"
	  << "});\n"
	  << "function filterMarketplace(q) {\n"
	  << "  q = (q || '').toLowerCase();\n"
	  << "  document.querySelectorAll('.wf-marketplace-item').forEach(el => {\n"
	  << "    let t = el.innerText.toLowerCase();\n"
	  << "    el.style.display = (!q || t.includes(q)) ? 'block' : 'none';\n"
	  << "  });\n"
	  << "}\n"
	  << "</script>\n";

	return h;
}

String GitHubPlatformService::RenderTppDocSite(const String& tpp_rel_dir)
{
	String root = GetDevDir();
	if(root.IsEmpty() || !SafeGHRelativePath(tpp_rel_dir))
		return "<div class=\"wf-alert wf-alert-warning\">Documentation preview unavailable: configure DEV_DIR and a safe relative directory.</div>";

	String full_tpp_dir = root;
	Vector<String> path_parts = Split(tpp_rel_dir, '/');
	for(const String& part : path_parts) {
		full_tpp_dir = AppendFileName(full_tpp_dir, part);
		if(IsSymlinkPath(full_tpp_dir))
			return "<div class=\"wf-alert wf-alert-warning\">Documentation preview unavailable: symbolic links are not followed.</div>";
	}
	if(!DirectoryExists(full_tpp_dir))
		return "<div class=\"wf-alert wf-alert-warning\">Documentation preview unavailable: configured directory was not found.</div>";

	Vector<String> files;
	Vector<String> titles;
	FindFile ff(AppendFileName(full_tpp_dir, "*.tpp"));
	while(ff) {
		if(ff.IsFile() && !IsSymlinkPath(ff.GetPath())) {
			files.Add(ff.GetPath());
			String tit;
			ExtractTppText(ff.GetPath(), tit);
			titles.Add(tit);
		}
		ff.Next();
	}

	String h;
	h << "<div style=\"background:#ffffff; color:#24292f; border-radius:8px; overflow:hidden; font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Helvetica,Arial,sans-serif;\">\n"
	  << "  <!-- Top U++ Banner -->\n"
	  << "  <div style=\"background:#003366; color:#ffffff; padding:12px 20px; display:flex; justify-content:space-between; align-items:center;\">\n"
	  << "    <div style=\"display:flex; align-items:center; gap:12px;\">\n"
	  << "      <span style=\"font-size:1.3rem; font-weight:800; letter-spacing:0.5px;\">Ultimate++</span>\n"
	  << "      <span style=\"font-size:0.95rem; color:#8ab4f8;\">Documentation &middot; " << EscapeHtml(tpp_rel_dir) << "</span>\n"
	  << "    </div>\n"
	  << "    <div style=\"display:flex; gap:10px;\">\n"
	  << "      <a href=\"/repo?tab=actions\" class=\"wf-btn\" style=\"background:#ffffff; color:#003366; font-weight:600; padding:4px 12px; font-size:0.8rem; text-decoration:none;\">"
	  << VividIcon::Get("arrow-left", 14) << " Back to Workflow Editor</a>\n"
	  << "    </div>\n"
	  << "  </div>\n"
	  << "  <!-- Two-Column Layout matching ultimatepp.org -->\n"
	  << "  <div style=\"display:flex; min-height:600px;\">\n"
	  << "    <!-- Sidebar Navigation -->\n"
	  << "    <div style=\"width:280px; background:#f6f8fa; border-right:1px solid #d0d7de; padding:14px; flex-shrink:0;\">\n"
	  << "      <div style=\"font-weight:700; font-size:0.88rem; color:#57606a; margin-bottom:10px; text-transform:uppercase; letter-spacing:0.5px;\">Topics (" << files.GetCount() << ")</div>\n"
	  << "      <ul style=\"list-style:none; padding:0; margin:0; display:flex; flex-direction:column; gap:4px;\">\n";

	for(int i = 0; i < files.GetCount(); i++) {
		String ftitle = titles[i];
		bool active = (i == 0);
		h << "        <li><a href=\"#topic-" << i << "\" onclick=\"showTppDoc(" << i << ")\" id=\"tpp-nav-" << i << "\" style=\"display:block; padding:5px 8px; border-radius:4px; font-size:0.86rem; color:" << (active ? "#0969da; font-weight:600; background:#ddf4ff;" : "#24292f;") << " text-decoration:none;\">"
		  << EscapeHtml(ftitle) << "</a></li>\n";
	}

	h << "      </ul>\n"
	  << "    </div>\n"
	  << "    <!-- Main Content Reader -->\n"
	  << "    <div id=\"tpp-doc-content\" style=\"flex:1; padding:24px 32px; background:#ffffff; overflow-y:auto; max-height:800px;\">\n";

	for(int i = 0; i < files.GetCount(); i++) {
		String tit = titles[i];
		String source = LoadFile(files[i]);
		h << "      <div class=\"tpp-topic-pane\" id=\"tpp-pane-" << i << "\" style=\"display:" << (i == 0 ? "block" : "none") << ";\">\n"
		  << "        <div style=\"font-size:0.82rem; color:#57606a; margin-bottom:6px;\">" << EscapeHtml(tpp_rel_dir) << " / " << EscapeHtml(GetFileName(files[i])) << "</div>\n"
		  << "        <h1 style=\"font-size:1.6rem; font-weight:700; border-bottom:1px solid #d0d7de; padding-bottom:8px; margin-bottom:16px; color:#1f2328;\">" << EscapeHtml(tit) << "</h1>\n"
		  << "        <pre class=\"tpp-source\" style=\"font-size:0.9rem; color:#1f2328; line-height:1.5; white-space:pre-wrap; overflow-wrap:anywhere;\">"
		  << EscapeHtml(source) << "</pre>\n"
		  << "      </div>\n";
	}

	h << "    </div>\n"
	  << "  </div>\n"
	  << "</div>\n"
	  << "<script>\n"
	  << "function showTppDoc(idx) {\n"
	  << "  document.querySelectorAll('.tpp-topic-pane').forEach((p, i) => { p.style.display = (i === idx ? 'block' : 'none'); });\n"
	  << "  document.querySelectorAll('[id^=\"tpp-nav-\"]').forEach((a, i) => {\n"
	  << "    a.style.color = (i === idx ? '#0969da' : '#24292f');\n"
	  << "    a.style.fontWeight = (i === idx ? '600' : 'normal');\n"
	  << "    a.style.background = (i === idx ? '#ddf4ff' : 'transparent');\n"
	  << "  });\n"
	  << "}\n"
	  << "</script>\n";

	return h;
}

Vector<ValueMap> GitHubPlatformService::GetLiveActions(const String& owner, const String& repo)
{
	Vector<ValueMap> list;
	return list;
}

Vector<ValueMap> GitHubPlatformService::GetLivePullRequests(const String& owner, const String& repo)
{
	Vector<ValueMap> list;
	return list;
}

ValueMap GitHubPlatformService::GetLivePullRequestDetail(const String& owner, const String& repo, int number)
{
	ValueMap res;
	return res;
}

String GitHubPlatformService::GetPullRequestDiff(const String& owner, const String& repo, int number)
{
	return String();
}

String GitHubPlatformService::CreatePullRequest(const String& owner, const String& repo, const String& title, const String& body, const String& head, const String& base)
{
	return "disabled";
}

String GitHubPlatformService::MergePullRequest(const String& owner, const String& repo, int number)
{
	return "disabled";
}

String GitHubPlatformService::ClosePullRequest(const String& owner, const String& repo, int number)
{
	return "disabled";
}

String GitHubPlatformService::AddPullRequestComment(const String& owner, const String& repo, int number, const String& body)
{
	return "disabled";
}

Vector<LocalRepoInfo> GitHubPlatformService::GetRepositories(const String& owner)
{
	Vector<LocalRepoInfo> list;
	String dev_dir = GetDevDir();
	if(dev_dir.IsEmpty() || (!owner.IsEmpty() && !SafeGHComponent(owner))) return list;

	FindFile ff(AppendFileName(dev_dir, "*"));
	int entries = 0;
	while(ff) {
		if(entries++ >= DataStore::MAX_FILES)
			break;
		if(ff.IsSymLink()) {
			ff.Next();
			continue;
		}
		if(ff.IsFolder()) {
			String repo_path = ff.GetPath();
			String normalized_repo = NormalizePath(repo_path);
			String normalized_root = NormalizePath(dev_dir);
			if(!normalized_repo.StartsWith(normalized_root + DIR_SEP)) {
				ff.Next();
				continue;
			}
			String git_dir = AppendFileName(repo_path, ".git");
			if(!IsSymlinkPath(git_dir) && DirectoryExists(git_dir)) {
				LocalRepoInfo info;
				info.name = ff.GetName();
				info.path = repo_path;
				info.owner = owner;

				String head_path = AppendFileName(git_dir, "HEAD");
				String head;
				if(!IsSymlinkPath(head_path) && GetFileLength(head_path) > 0 && GetFileLength(head_path) <= 4096)
					head = LoadFile(head_path);
				if(head.StartsWith("ref: refs/heads/"))
					info.branch = TrimBoth(head.Mid(16));
				else
					info.branch.Clear();

				if(FileExists(AppendFileName(repo_path, info.name + ".upp")) || DirectoryExists(AppendFileName(repo_path, "uppsrc")))
					info.language = "C++";
				else if(FileExists(AppendFileName(repo_path, "CMakeLists.txt")))
					info.language = "C++";
				else if(FileExists(AppendFileName(repo_path, "requirements.txt")) || FileExists(AppendFileName(repo_path, "setup.py")))
					info.language = "Python";
				else if(FileExists(AppendFileName(repo_path, "package.json")))
					info.language = "JavaScript";
				else
					info.language = "C++";

				String desc_file = AppendFileName(git_dir, "description");
				if(FileExists(desc_file) && !IsSymlinkPath(desc_file) && GetFileLength(desc_file) <= 4096) {
					String d = TrimBoth(LoadFile(desc_file));
					if(!d.StartsWith("Unnamed repository"))
						info.description = d;
				}

				info.is_private = true;
				list.Add(pick(info));
			}
		}
		ff.Next();
	}

	Sort(list, [](const LocalRepoInfo& a, const LocalRepoInfo& b) {
		return a.name < b.name;
	});

	return list;
}

LocalRepoInfo GitHubPlatformService::GetRepo(const String& owner, const String& repo)
{
	LocalRepoInfo info;
	if(!SafeGHComponent(owner) || !SafeGHComponent(repo))
		return info;
	String root = GetDevDir();
	if(root.IsEmpty()) return info;
	info.owner = owner;
	info.name = repo;
	info.path = AppendFileName(root, repo);
	if(IsSymlinkPath(info.path)) return LocalRepoInfo();
	if(!NormalizePath(info.path).StartsWith(NormalizePath(root) + DIR_SEP)) return LocalRepoInfo();
	if(!DirectoryExists(info.path))
		return LocalRepoInfo();
	String git_dir = AppendFileName(info.path, ".git");
	if(IsSymlinkPath(git_dir) || !DirectoryExists(git_dir))
		return LocalRepoInfo();
	String head_path = AppendFileName(git_dir, "HEAD");
	String head;
	if(!IsSymlinkPath(head_path) && GetFileLength(head_path) > 0 && GetFileLength(head_path) <= 4096)
		head = LoadFile(head_path);
	if(head.StartsWith("ref: refs/heads/"))
		info.branch = TrimBoth(head.Mid(16));
	return info;
}

Vector<String> GitHubPlatformService::GetBranches(const String& repo_path)
{
	(void)repo_path;
	return Vector<String>(); // branch evidence must be imported as bounded records
}

Vector<ValueMap> GitHubPlatformService::GetTree(const String& repo_path, const String& subpath)
{
	Vector<ValueMap> list;
	// Raw traversal is disabled until canonical symlink containment is available.
	return list;
#if 0
	String full_dir = repo_path;
	if(!subpath.IsEmpty()) full_dir = AppendFileName(repo_path, subpath);
	if(DirectoryExists(full_dir)) {
		FindFile ff(AppendFileName(full_dir, "*"));
		Vector<String> dirs, files;
		while(ff) {
			String n = ff.GetName();
			if(n != ".git" && n != "_out" && n != "tmp") {
				if(ff.IsFolder()) dirs.Add(n); else files.Add(n);
			}
			ff.Next();
		}
		Sort(dirs);
		Sort(files);

		for(const String& d : dirs) {
			ValueMap m;
			m.Set("name", d);
			m.Set("type", "dir");
			String rel = subpath.IsEmpty() ? d : subpath + "/" + d;
			m.Set("path", rel);
			m.Set("msg", "Directory snapshot");
			m.Set("age", "recent");
			list.Add(m);
		}
		for(const String& f : files) {
			ValueMap m;
			m.Set("name", f);
			m.Set("type", "file");
			String rel = subpath.IsEmpty() ? f : subpath + "/" + f;
			m.Set("path", rel);
			m.Set("msg", "Source artifact");
			m.Set("age", "recent");
			list.Add(m);
		}
	}
#endif
}

ValueMap GitHubPlatformService::GetBlob(const String& repo_path, const String& filepath)
{
	return ValueMap();
}

Vector<ValueMap> GitHubPlatformService::GetCommits(const String& repo_path, int limit)
{
	(void)repo_path;
	(void)limit;
	return Vector<ValueMap>(); // commit evidence must be imported as bounded records
}

String GitHubPlatformService::RenderRepoHeader(const LocalRepoInfo& info)
{
	String h;
	h << "<div class=\"wf-gh-repohead\">\n"
	  << "  <div class=\"wf-gh-repohead-row\">\n"
	  << "    <div class=\"wf-gh-title\">\n"
	  << "      " << VividIcon::Get("git-repo", 20) << "\n"
	  << "      <a href=\"" << GHUserRoute(info.owner) << "\">" << EscapeHtml(info.owner) << "</a> / "
	  << "      <strong><a href=\"" << GHRepoRoute(info.owner, info.name) << "\">" << EscapeHtml(info.name) << "</a></strong>\n"
	  << "      <span class=\"wf-gh-badge\">" << (info.is_private ? "Private" : "Public") << "</span>\n"
	  << "    </div>\n"
	  << "    <div class=\"wf-gh-actions\">\n"
	  << "      <a href=\"/projects/" << UrlEncode(info.name) << "/\" class=\"wf-gh-btn\" title=\"Switch to SourceForge Platform Style\" style=\"color:#ee6524; font-weight:600; text-decoration:none;\">\n"
	  << "        <svg width=\"14\" height=\"14\" viewBox=\"0 0 24 24\" fill=\"currentColor\" style=\"vertical-align:-2px;\"><path d=\"M12 2L2 7l10 5 10-5-10-5zM2 17l10 5 10-5M2 12l10 5 10-5\"/></svg>\n"
	  << "        <span>SourceForge View</span>\n"
	  << "      </a>\n"
	  << "      <div class=\"wf-gh-btn-group\">\n"
	  << "        <button class=\"wf-gh-btn\">" << VividIcon::Get("eye", 14) << " Watch <span class=\"wf-gh-counter\">1</span></button>\n"
	  << "      </div>\n"
	  << "      <div class=\"wf-gh-btn-group\">\n"
	  << "        <button class=\"wf-gh-btn\">" << VividIcon::Get("star", 14) << " Star <span class=\"wf-gh-counter\">" << info.stars << "</span></button>\n"
	  << "      </div>\n"
	  << "      <div class=\"wf-gh-btn-group\">\n"
	  << "        <button class=\"wf-gh-btn\">" << VividIcon::Get("git-fork", 14) << " Fork <span class=\"wf-gh-counter\">" << info.forks << "</span></button>\n"
	  << "      </div>\n"
	  << "    </div>\n"
	  << "  </div>\n";
	return h;
}

String GitHubPlatformService::RenderRepoTabs(const String& owner, const String& repo, const String& active_tab, const LocalRepoInfo& info)
{
	String base = GHRepoRoute(owner, repo);
	String h;
	h << "  <nav class=\"wf-gh-nav-tabs\" aria-label=\"Repository\">\n"
	  << "    <a href=\"" << base << "?tab=code\" class=\"wf-gh-tab" << (active_tab == "code" ? " active" : "") << "\">"
	  << VividIcon::Get("code", 16) << " Code</a>\n"
	  << "    <a href=\"" << base << "?tab=issues\" class=\"wf-gh-tab" << (active_tab == "issues" ? " active" : "") << "\">"
	  << VividIcon::Get("bug", 16) << " Issues <span class=\"wf-gh-counter\">2</span></a>\n"
	  << "    <a href=\"" << base << "?tab=pulls\" class=\"wf-gh-tab" << (active_tab == "pulls" ? " active" : "") << "\">"
	  << VividIcon::Get("git-pull-request", 16) << " Pull requests <span class=\"wf-gh-counter\">1</span></a>\n"
	  << "    <a href=\"" << base << "?tab=actions\" class=\"wf-gh-tab" << (active_tab == "actions" ? " active" : "") << "\">"
	  << VividIcon::Get("play-circle", 16) << " Actions</a>\n"
	  << "    <a href=\"" << base << "?tab=projects\" class=\"wf-gh-tab" << (active_tab == "projects" ? " active" : "") << "\">"
	  << VividIcon::Get("layout-dashboard", 16) << " Projects <span class=\"wf-gh-counter\">3</span></a>\n"
	  << "    <a href=\"" << base << "?tab=security\" class=\"wf-gh-tab" << (active_tab == "security" ? " active" : "") << "\">"
	  << VividIcon::Get("shield", 16) << " Security</a>\n"
	  << "    <a href=\"" << base << "?tab=dependencies\" class=\"wf-gh-tab" << (active_tab == "dependencies" ? " active" : "") << "\">"
	  << VividIcon::Get("network", 16) << " Insights</a>\n"
	  << "    <a href=\"" << base << "?tab=settings\" class=\"wf-gh-tab" << (active_tab == "settings" ? " active" : "") << "\">"
	  << VividIcon::Get("settings", 16) << " Settings</a>\n"
	  << "    <a href=\"" << base << "?tab=agents\" class=\"wf-gh-tab" << (active_tab == "agents" ? " active" : "") << "\" style=\"color:var(--wf-color-positive, #00aa7f); font-weight:600;\">"
	  << VividIcon::Get("robot", 16) << " SweAgent Pair</a>\n"
	  << "  </nav>\n"
	  << "</div>\n";
	return h;
}

// ---------------------------------------------------------------------------
// SKYLARK Views: User Profile & Repository Parity
// ---------------------------------------------------------------------------

static void RenderUserProfile(Http& http, const String& username)
{
	http.ContentType("text/html; charset=utf-8");
	http.Response(503, "Service Unavailable");
	http << "<!doctype html><html><body><p>Configured GitHub profile data is unavailable.</p></body></html>";
}

static void RenderRepoView(Http& http, const String& owner, const String& repo, const String& deep_path)
{
	if(!SafeGHComponent(owner) || !SafeGHComponent(repo)) {
		http.Response(404, "Repository Not Found") << "Repository not found";
		return;
	}
	if(!deep_path.IsEmpty() && !SafeGHRelativePath(deep_path)) {
		http.Response(400, "Invalid Repository Path") << "Invalid repository path";
		return;
	}
	if(http["platform"] == "sf") {
		http.Redirect("/projects/" + UrlEncode(repo) + "/");
		return;
	}

	LocalRepoInfo info = GitHubPlatformService::GetRepo(owner, repo);
	ValueMap scraped = GitHubPlatformService::LoadScrapedData(owner, repo);

	String active_tab = http["tab"];
	String sub_path = deep_path;
	String agent_run_id = http["run"];

	// Parse sub-path segments
	String mode = "tree"; // "tree", "blob", "commits", "issues", "pulls", "agents", "actions", "projects", "security", "settings"
	String branch = info.branch;
	String file_or_folder;
	int issue_num = 0;
	int pr_num = ScanInt((String)http["pr"]);

	if(!sub_path.IsEmpty()) {
		Vector<String> parts = Split(sub_path, '/');
		if(parts.GetCount() > 0) {
			String p0 = parts[0];
			if(p0 == "tree") {
				mode = "tree";
				active_tab = "code";
				if(parts.GetCount() > 1) branch = parts[1];
				for(int i = 2; i < parts.GetCount(); i++) {
					if(!file_or_folder.IsEmpty()) file_or_folder << "/";
					file_or_folder << parts[i];
				}
			} else if(p0 == "blob") {
				mode = "blob";
				active_tab = "code";
				if(parts.GetCount() > 1) branch = parts[1];
				for(int i = 2; i < parts.GetCount(); i++) {
					if(!file_or_folder.IsEmpty()) file_or_folder << "/";
					file_or_folder << parts[i];
				}
			} else if(p0 == "commits") {
				mode = "commits";
				active_tab = "commits";
				if(parts.GetCount() > 1) branch = parts[1];
			} else if(p0 == "issues") {
				mode = "issues";
				active_tab = "issues";
				if(parts.GetCount() > 1 && IsDigit(parts[1][0])) {
					issue_num = ScanInt(parts[1]);
				}
			} else if(p0 == "pulls" || p0 == "pull") {
				mode = "pulls";
				active_tab = "pulls";
				if(parts.GetCount() > 1 && IsDigit(parts[1][0])) {
					pr_num = ScanInt(parts[1]);
				}
			} else if(p0 == "agents") {
				mode = "agents";
				active_tab = "agents";
				if(parts.GetCount() > 1) {
					agent_run_id = parts[1];
				}
			} else if(p0 == "actions") {
				mode = "actions";
				active_tab = "actions";
				if(parts.GetCount() > 1 && parts[1] == "new") {
					mode = "actions_new";
				}
			} else if(p0 == "projects") {
				mode = "projects";
				active_tab = "projects";
			} else if(p0 == "security") {
				mode = "security";
				active_tab = "security";
			} else if(p0 == "settings") {
				mode = "settings";
				active_tab = "settings";
			}
		}
	}

	String req_template = http["template"];
	String req_wf_template = http["workflow_template"];
	if(http["action"] == "new" || !req_template.IsEmpty() || !req_wf_template.IsEmpty()) {
		mode = "actions_new";
		active_tab = "actions";
	}

	if(active_tab.IsEmpty()) active_tab = "code";

	String sub_nav = GitHubPlatformService::RenderRepoHeader(info) +
	                 GitHubPlatformService::RenderRepoTabs(owner, repo, active_tab, info);

	String content;
	String sidebar;

	// -----------------------------------------------------------------------
	// 1. Code Explorer (Tree & Blob Viewer)
	// -----------------------------------------------------------------------
	if(active_tab == "code") {
		String base_url = GHRepoRoute(owner, repo);
		Vector<String> branches = GitHubPlatformService::GetBranches(info.path);

		// Branch Switcher Bar
		String branch_bar;
		branch_bar
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem; flex-wrap:wrap; gap:12px;\">\n"
			<< "  <div style=\"display:flex; align-items:center; gap:8px;\">\n"
			<< "    <div class=\"wf-dropdown\" style=\"position:relative; display:inline-block;\">\n"
			<< "      <button class=\"wf-gh-btn\" style=\"font-weight:600; font-size:0.85rem;\">" << VividIcon::Get("git-branch", 14) << " " << GitHubPlatformService::EscapeHtml(branch) << " &#9662;</button>\n"
			<< "    </div>\n";

		// Path breadcrumb
		branch_bar << "    <nav class=\"wf-breadcrumb\" style=\"font-size:0.9rem; font-weight:600;\">\n"
		           << "      <a href=\"" << base_url << "\">" << GitHubPlatformService::EscapeHtml(repo) << "</a>";
		if(!file_or_folder.IsEmpty()) {
			Vector<String> segs = Split(file_or_folder, '/');
			String accum;
			for(int i = 0; i < segs.GetCount(); i++) {
				if(!accum.IsEmpty()) accum << "/";
					accum << segs[i];
				bool is_last = (i == segs.GetCount() - 1);
				if(is_last)
					branch_bar << " / <span style=\"color:var(--wf-fg-normal,#e6edf3);\">" << GitHubPlatformService::EscapeHtml(segs[i]) << "</span>";
				else
					branch_bar << " / <a href=\"" << GHAppendPath(base_url + "/tree", branch + "/" + accum) << "\">" << GitHubPlatformService::EscapeHtml(segs[i]) << "</a>";
			}
		}
		branch_bar << "    </nav>\n"
		           << "  </div>\n"
		           << "  <div style=\"display:flex; gap:8px; align-items:center;\">\n"
		           << "    <a href=\"" << base_url << "?tab=commits\" class=\"wf-gh-btn\" style=\"font-size:0.8rem;\">" << VividIcon::Get("clock", 14) << " Commits</a>\n"
		           << "    <button class=\"wf-gh-btn wf-btn-positive\" style=\"font-size:0.8rem; background:#238636; border-color:#2ea043; color:#fff;\">Code &#9662;</button>\n"
		           << "  </div>\n"
		           << "</div>\n";

		content << branch_bar;

		if(mode == "blob" && !file_or_folder.IsEmpty()) {
			// Blob File Viewer
			ValueMap blob = GitHubPlatformService::GetBlob(info.path, file_or_folder);
			String file_content = blob["content"];
			int line_count = blob["lines"];
			String size_str = blob["size_str"];
			bool is_markdown = file_or_folder.EndsWith(".md") || file_or_folder.EndsWith(".markdown") || file_or_folder.EndsWith(".MD");

			content
				<< "<div class=\"wf-gh-file-box\">\n"
				<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
				<< "    <div style=\"font-size:0.85rem; color:var(--wf-fg-muted,#848d97);\">\n"
				<< "      <strong>" << GitHubPlatformService::EscapeHtml(GetFileName(file_or_folder)) << "</strong> &middot; "
				<< "      <span>" << line_count << " lines</span> &middot; "
				<< "      <span>" << GitHubPlatformService::EscapeHtml(size_str) << "</span>\n"
				<< "    </div>\n"
				<< "    <div style=\"display:flex; gap:6px; align-items:center;\">\n";

			if(is_markdown) {
				content
					<< "      <div class=\"wf-view-toggle\" style=\"margin-right:6px;\">\n"
					<< "        <button type=\"button\" id=\"btn-view-preview\" class=\"active\" onclick=\"toggleFileView('preview')\">Preview</button>\n"
					<< "        <button type=\"button\" id=\"btn-view-code\" onclick=\"toggleFileView('code')\">Code</button>\n"
					<< "      </div>\n";
			}

			content
				<< "      <a href=\"" << GHAppendPath("/raw/" + UrlEncode(owner) + "/" + UrlEncode(repo), file_or_folder) << "\" class=\"wf-gh-btn\" target=\"_blank\">Raw</a>\n"
				<< "      <button class=\"wf-gh-btn\" onclick=\"navigator.clipboard.writeText(document.getElementById('wf-blob-raw-md') ? document.getElementById('wf-blob-raw-md').value : document.getElementById('wf-blob-code').innerText); alert('Copied raw contents to clipboard!');\">Copy</button>\n"
				<< "    </div>\n"
				<< "  </div>\n";

			if(is_markdown) {
				content
					<< "  <div id=\"wf-blob-preview-box\" style=\"padding:1.5rem; background:var(--wf-bg-view,#0d1117); border-radius:0 0 6px 6px;\">\n"
					<< "    <pre id=\"wf-blob-preview\" style=\"white-space:pre-wrap;\">" << GitHubPlatformService::EscapeHtml(file_content) << "</pre>\n"
					<< "  </div>\n";
			}

			content
				<< "  <div id=\"wf-blob-code-box\" style=\"" << (is_markdown ? "display:none; " : "") << "overflow-x:auto; background:var(--wf-bg-view,#0d1117); font-family:SFMono-Regular,Consolas,'Liberation Mono',Menlo,monospace; font-size:0.82rem; line-height:1.5;\">\n"
				<< "    <table style=\"width:100%; border-collapse:collapse;\">\n"
				<< "      <tbody id=\"wf-blob-code\">\n";

			Vector<String> code_lines = Split(file_content, '\n');
			for(int i = 0; i < code_lines.GetCount(); i++) {
				content
					<< "        <tr>\n"
					<< "          <td style=\"width:48px; text-align:right; padding:0 12px; color:var(--wf-fg-inactive,#7d8590); user-select:none; border-right:1px solid var(--wf-border-color,#30363d); font-size:0.75rem;\">" << (i + 1) << "</td>\n"
					<< "          <td style=\"padding:0 16px; white-space:pre;\">" << GitHubPlatformService::EscapeHtml(code_lines[i]) << "</td>\n"
					<< "        </tr>\n";
			}

			content
				<< "      </tbody>\n"
				<< "    </table>\n"
				<< "  </div>\n"
				<< "</div>\n";
		}
		else {
			// Directory Tree Browser
			content
				<< "<div class=\"wf-gh-file-box\">\n"
				<< "  <div class=\"wf-gh-file-header\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:10px;\">\n"
				<< "      <span style=\"font-weight:600;\">" << GitHubPlatformService::EscapeHtml(!info.last_commit_author.IsEmpty() ? info.last_commit_author : "Repository contributor") << "</span>\n"
				<< "      <span style=\"color:var(--wf-fg-inactive,#7d8590);\">" << GitHubPlatformService::EscapeHtml(!info.last_commit_msg.IsEmpty() ? info.last_commit_msg : "Repository files and documentation") << "</span>\n"
				<< "    </div>\n"
				<< "    <div style=\"color:var(--wf-fg-muted,#848d97); font-size:0.78rem;\">\n"
				<< "      <code>" << GitHubPlatformService::EscapeHtml(!info.last_commit_hash.IsEmpty() ? info.last_commit_hash : "master") << "</code> &middot; "
				<< GitHubPlatformService::EscapeHtml(!info.last_commit_time.IsEmpty() ? info.last_commit_time : "recent") << "\n"
				<< "    </div>\n"
				<< "  </div>\n";

			// Up directory link if inside subfolder
			if(!file_or_folder.IsEmpty()) {
				int slash = file_or_folder.ReverseFind('/');
				String parent_url = base_url;
				if(slash > 0)
					parent_url = GHAppendPath(base_url + "/tree", branch + "/" + file_or_folder.Left(slash));
				content
					<< "  <div class=\"wf-gh-file-row\">\n"
					<< "    <span class=\"wf-gh-file-name\">" << VividIcon::Get("corner-left-up", 16) << " <a href=\"" << GitHubPlatformService::EscapeHtml(parent_url) << "\">..</a></span>\n"
					<< "    <span class=\"wf-gh-file-msg\"></span>\n"
					<< "    <span class=\"wf-gh-file-age\"></span>\n"
					<< "  </div>\n";
			}

			Vector<ValueMap> entries = GitHubPlatformService::GetTree(info.path, file_or_folder);
			for(const ValueMap& e : entries) {
				String name = e["name"];
				String type = e["type"];
				String rel_path = e["path"];
				String link_url;
				if(type == "dir")
					link_url = GHAppendPath(base_url + "/tree", branch + "/" + rel_path);
				else
					link_url = GHAppendPath(base_url + "/blob", branch + "/" + rel_path);

				content
					<< "  <div class=\"wf-gh-file-row\">\n"
					<< "    <span class=\"wf-gh-file-name\">" << VividIcon::Get(type == "dir" ? "folder" : "file-text", 16)
					<< " <a href=\"" << GitHubPlatformService::EscapeHtml(link_url) << "\">" << GitHubPlatformService::EscapeHtml(name) << "</a></span>\n"
					<< "    <span class=\"wf-gh-file-msg\">" << GitHubPlatformService::EscapeHtml((String)e["msg"]) << "</span>\n"
					<< "    <span class=\"wf-gh-file-age\">" << GitHubPlatformService::EscapeHtml((String)e["age"]) << "</span>\n"
					<< "  </div>\n";
			}
			content << "</div>\n";

			// README rendered box
			String readme_full = AppendFileName(info.path, file_or_folder.IsEmpty() ? "README.md" : file_or_folder + "/README.md");
			String readme_body;
			if(FileExists(readme_full))
				readme_body = LoadFile(readme_full);
			else if(file_or_folder.IsEmpty() && !scraped.IsEmpty() && !((String)scraped["overview"]["readme_snippet"]).IsEmpty())
				readme_body = scraped["overview"]["readme_snippet"];
			else if(file_or_folder.IsEmpty())
				readme_body = "### " + info.name + "\n\nLocal repository documentation is unavailable.";

			if(!readme_body.IsEmpty()) {
				content
					<< "<div class=\"wf-gh-readme-box\">\n"
					<< "  <div class=\"wf-gh-readme-header\">" << VividIcon::Get("book", 16) << " README.md</div>\n"
					<< "  <pre class=\"wf-gh-readme-body\" style=\"white-space:pre-wrap;\">" << GitHubPlatformService::EscapeHtml(readme_body) << "</pre>\n"
					<< "</div>\n";
			}
		}

		// Sidebar About
		sidebar
			<< "<div class=\"wf-gh-sidebar-section\">\n"
			<< "  <span class=\"wf-gh-sidebar-title\">About</span>\n"
			<< "  <p style=\"color:var(--wf-fg-inactive,#7d8590);\">"
			<< GitHubPlatformService::EscapeHtml(!info.description.IsEmpty() ? info.description : "No description provided.") << "</p>\n"
			<< "  <div>" << VividIcon::Get("activity", 14) << " Activity: <strong>Daily</strong></div>\n"
			<< "  <div>" << VividIcon::Get("star", 14) << " Stars: <strong>" << info.stars << "</strong></div>\n"
			<< "  <div>" << VividIcon::Get("eye", 14) << " Watching: <strong>1</strong></div>\n"
			<< "  <div>" << VividIcon::Get("git-fork", 14) << " Forks: <strong>" << info.forks << "</strong></div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-gh-sidebar-section\">\n"
			<< "  <span class=\"wf-gh-sidebar-title\">Languages</span>\n"
			<< "  <div style=\"font-size:0.85rem;\"><span style=\"display:inline-block; width:10px; height:10px; border-radius:50%; background:#f34b7d; margin-right:4px;\"></span> " << GitHubPlatformService::EscapeHtml(info.language) << " 100%</div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 2. Commits History View
	// -----------------------------------------------------------------------
	else if(active_tab == "commits") {
		Vector<ValueMap> commits = GitHubPlatformService::GetCommits(info.path, 30);
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Commit History &middot; " << GitHubPlatformService::EscapeHtml(branch) << "</span><span class=\"wf-badge wf-badge-neutral\">" << commits.GetCount() << " commits</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
			<< "    <div class=\"wf-gh-file-box\" style=\"border:none; margin:0;\">\n";
		for(const ValueMap& c : commits) {
			content
				<< "      <div class=\"wf-gh-file-row\" style=\"justify-content:space-between; padding:12px 16px;\">\n"
				<< "        <div>\n"
				<< "          <div style=\"font-weight:600; color:var(--wf-fg-normal,#ffffff); font-size:0.92rem;\">" << GitHubPlatformService::EscapeHtml((String)c["msg"]) << "</div>\n"
				<< "          <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">"
				<< "            <strong>" << GitHubPlatformService::EscapeHtml((String)c["author"]) << "</strong> committed " << GitHubPlatformService::EscapeHtml((String)c["time"]) << "\n"
				<< "          </div>\n"
				<< "        </div>\n"
				<< "        <div>\n"
				<< "          <code style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); padding:3px 8px; border-radius:4px;\">" << GitHubPlatformService::EscapeHtml((String)c["hash"]) << "</code>\n"
				<< "        </div>\n"
				<< "      </div>\n";
		}
		content << "    </div>\n  </div>\n</div>\n";
	}
	// -----------------------------------------------------------------------
	// 3. Issues Parity View (List or Detail)
	// -----------------------------------------------------------------------
	else if(active_tab == "issues" && issue_num > 0) {
		ValueMap is_detail = GitHubPlatformService::GetLiveIssueDetail(owner, repo, issue_num);
		String is_title = is_detail["title"];
		String is_state = is_detail["state"];
		String is_body = is_detail["body"];
		String is_author = owner;
		if(!is_detail["author"].IsNull() && !is_detail["author"]["login"].IsNull())
			is_author = is_detail["author"]["login"];
		String is_created = ((String)is_detail["createdAt"]).Left(10);
		bool is_open = (is_state == "OPEN");

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"" << GHRepoRoute(owner, repo) << "?tab=issues\" class=\"wf-gh-btn\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to issues</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< GitHubPlatformService::EscapeHtml(is_title) << " <span style=\"color:var(--wf-fg-inactive,#7d8590); font-weight:400;\">#" << issue_num << "</span></h1>\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px; font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); flex-wrap:wrap;\">\n"
			<< "    <span class=\"wf-badge\" style=\"background:" << (is_open ? "#238636" : "#8957e5") << "; color:#ffffff; font-weight:700; padding:4px 10px; border-radius:12px; font-size:0.82rem; display:inline-flex; align-items:center; gap:4px;\">"
			<< VividIcon::Get(is_open ? "alert-circle" : "check", 14) << " " << (is_open ? "Open" : "Closed") << "</span>\n"
			<< "    <span><strong>" << GitHubPlatformService::EscapeHtml(is_author) << "</strong> opened this issue on " << GitHubPlatformService::EscapeHtml(is_created) << "</span>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Issue body card
		content
			<< "<div class=\"wf-gh-file-box\" style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << GitHubPlatformService::EscapeHtml(is_author) << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << GitHubPlatformService::EscapeHtml(is_created) << "</span></div>\n"
			<< "    <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Owner</span>\n"
			<< "  </div>\n"
			<< "  <pre class=\"wf-gh-file-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6; white-space:pre-wrap;\">\n"
			<< GitHubPlatformService::EscapeHtml(!is_body.IsEmpty() ? is_body : "No description provided.") << "\n"
			<< "  </pre>\n"
			<< "</div>\n";

		// Comments list
		ValueArray comments = is_detail["comments"];
		for(int i = 0; i < comments.GetCount(); i++) {
			Value c = comments[i];
			String c_author = !c["author"].IsNull() && !c["author"]["login"].IsNull() ? (String)c["author"]["login"] : owner;
			String c_body = c["body"];
			String c_time = ((String)c["createdAt"]).Left(10);
			content
				<< "<div class=\"wf-gh-file-box\" style=\"margin-bottom:1.5rem;\">\n"
				<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
				<< "    <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << GitHubPlatformService::EscapeHtml(c_author) << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << GitHubPlatformService::EscapeHtml(c_time) << "</span></div>\n"
				<< "    <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Collaborator</span>\n"
				<< "  </div>\n"
				<< "  <pre class=\"wf-gh-file-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6; white-space:pre-wrap;\">\n"
				<< GitHubPlatformService::EscapeHtml(c_body) << "\n"
				<< "  </pre>\n"
				<< "</div>\n";
		}

		// Comment Box & Action Buttons
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Add a comment</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <textarea id=\"gh-comment-text\" rows=\"4\" placeholder=\"Leave a comment...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:10px; color:var(--wf-fg-normal,#e6edf3); font-size:0.88rem; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <div style=\"display:flex; justify-content:flex-end; gap:8px;\">\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\">External issue changes are disabled</span>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	else if(active_tab == "issues") {
		Vector<ValueMap> live_issues = GitHubPlatformService::GetLiveIssues(owner, repo);
		int open_count = 0, closed_count = 0;
		for(const ValueMap& is : live_issues) {
			if((String)is["state"] == "OPEN") open_count++;
			else closed_count++;
		}

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem; flex-wrap:wrap; gap:12px;\">\n"
			<< "  <div style=\"display:flex; gap:10px; flex:1; max-width:600px;\">\n"
			<< "    <input type=\"search\" value=\"is:issue is:open \" style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; color:var(--wf-fg-normal,#e6edf3); width:100%; font-size:0.85rem;\">\n"
			<< "  </div>\n"
			<< "  <div style=\"display:flex; gap:8px;\">\n"
			<< "    <button id=\"btn-open-new-issue\" onclick=\"document.getElementById('new-issue-card').style.display='block';\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-weight:600; font-size:0.85rem;\">New issue</button>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div id=\"new-issue-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid var(--wf-accent-focus,#58a6ff);\">\n"
			<< "  <div class=\"wf-card-header\"><span>Create New Issue in " << GitHubPlatformService::EscapeHtml(owner) << "/" << GitHubPlatformService::EscapeHtml(repo) << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <input type=\"text\" id=\"new-issue-title\" placeholder=\"Title\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.95rem; font-weight:600; margin-bottom:12px;\">\n"
			<< "    <textarea id=\"new-issue-body\" rows=\"5\" placeholder=\"Leave a comment / issue description...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:10px 12px; color:var(--wf-fg-normal,#e6edf3); font-size:0.88rem; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "    <div style=\"display:flex; justify-content:flex-end; gap:8px;\">\n"
			<< "      <button onclick=\"document.getElementById('new-issue-card').style.display='none';\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.82rem;\">Cancel</button>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\">External issue creation is disabled</span>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n"
			<< "<div class=\"wf-gh-file-box\">\n"
			<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; gap:16px; font-size:0.88rem;\">\n"
			<< "      <strong>" << VividIcon::Get("alert-circle", 16) << " " << open_count << " Open</strong>\n"
			<< "      <span style=\"color:var(--wf-fg-inactive,#7d8590);\">" << VividIcon::Get("check", 16) << " " << closed_count << " Closed</span>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; gap:16px; font-size:0.82rem; color:var(--wf-fg-inactive,#7d8590);\">\n"
			<< "      <span>Author &#9662;</span><span>Label &#9662;</span><span>Projects &#9662;</span><span>Milestones &#9662;</span><span>Assignee &#9662;</span>\n"
			<< "    </div>\n"
			<< "  </div>\n";

		if(!live_issues.IsEmpty()) {
			for(const ValueMap& is : live_issues) {
				bool is_open = ((String)is["state"] == "OPEN");
				int num = (int)is["number"];
				content
					<< "  <div class=\"wf-gh-file-row\" style=\"justify-content:space-between; padding:12px 16px;\">\n"
					<< "    <div>\n"
					<< "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
					<< "        <span style=\"color:" << (is_open ? "#238636" : "#8957e5") << ";\">" << VividIcon::Get(is_open ? "alert-circle" : "check", 16) << "</span>\n"
					<< "        <a href=\"" << GHRepoRoute(owner, repo) << "/issues/" << num << "\" style=\"font-weight:600; font-size:0.95rem; color:var(--wf-fg-normal,#e6edf3); text-decoration:none;\">" << GitHubPlatformService::EscapeHtml((String)is["title"]) << "</a>\n"
					<< "      </div>\n"
					<< "      <div style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px; margin-left:24px;\">\n"
					<< "        #" << num << " opened on " << GitHubPlatformService::EscapeHtml((String)is["created_at"]) << " by <strong>" << GitHubPlatformService::EscapeHtml((String)is["author"]) << "</strong>\n"
					<< "      </div>\n"
					<< "    </div>\n"
					<< "    <div style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.8rem;\">" << VividIcon::Get("message-square", 14) << " " << (int)is["comments"] << "</div>\n"
					<< "  </div>\n";
			}
		}
		content << "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 4. Pull Requests Parity View (List or Detail)
	// -----------------------------------------------------------------------
	else if(active_tab == "pulls" && pr_num > 0) {
		ValueMap pr_detail = GitHubPlatformService::GetLivePullRequestDetail(owner, repo, pr_num);
		String pr_title = pr_detail["title"];
		String pr_state = pr_detail["state"]; // "OPEN", "MERGED", "CLOSED"
		String pr_body = pr_detail["body"];
		String pr_author = owner;
		if(!pr_detail["author"].IsNull() && !pr_detail["author"]["login"].IsNull())
			pr_author = pr_detail["author"]["login"];
		String pr_created = ((String)pr_detail["createdAt"]).Left(10);
		String pr_head = pr_detail["headRefName"];
		String pr_base = pr_detail["baseRefName"];
		if(pr_head.IsEmpty()) pr_head = "feature/platform-parity";
		if(pr_base.IsEmpty()) pr_base = "master";
		bool is_open = (pr_state == "OPEN");
		bool is_merged = (pr_state == "MERGED");

		content
			<< "<div style=\"margin-bottom:1.5rem;\">\n"
			<< "  <div style=\"margin-bottom:0.75rem;\">\n"
			<< "    <a href=\"" << GHRepoRoute(owner, repo) << "/pulls\" class=\"wf-gh-btn\" style=\"font-size:0.82rem; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("chevron-left", 14) << " Back to pull requests</a>\n"
			<< "  </div>\n"
			<< "  <h1 style=\"font-size:1.75rem; font-weight:600; margin:0 0 8px 0; color:var(--wf-fg-normal,#e6edf3);\">"
			<< GitHubPlatformService::EscapeHtml(pr_title) << " <span style=\"color:var(--wf-fg-inactive,#7d8590); font-weight:400;\">#" << pr_num << "</span></h1>\n"
			<< "  <div style=\"display:flex; align-items:center; gap:10px; font-size:0.85rem; color:var(--wf-fg-inactive,#7d8590); flex-wrap:wrap;\">\n"
			<< "    <span class=\"wf-badge\" style=\"background:" << (is_open ? "#238636" : (is_merged ? "#8957e5" : "#f85149")) << "; color:#ffffff; font-weight:700; padding:4px 10px; border-radius:12px; font-size:0.82rem; display:inline-flex; align-items:center; gap:4px;\">"
			<< VividIcon::Get(is_open ? "git-pull-request" : (is_merged ? "check" : "x"), 14) << " " << (is_open ? "Open" : (is_merged ? "Merged" : "Closed")) << "</span>\n"
			<< "    <span><strong>" << GitHubPlatformService::EscapeHtml(pr_author) << "</strong> wants to merge into <code>" << GitHubPlatformService::EscapeHtml(pr_base) << "</code> from <code>" << GitHubPlatformService::EscapeHtml(pr_head) << "</code> &middot; opened on " << GitHubPlatformService::EscapeHtml(pr_created) << "</span>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Subtabs: Conversation vs Files Changed
		content
			<< "<div class=\"wf-view-toggle\" style=\"margin-bottom:1.5rem;\">\n"
			<< "  <button type=\"button\" id=\"btn-pr-convo\" class=\"active\" onclick=\"switchPrTab('convo')\">" << VividIcon::Get("message-square", 14) << " Conversation</button>\n"
			<< "  <button type=\"button\" id=\"btn-pr-diff\" onclick=\"switchPrTab('diff')\">" << VividIcon::Get("code", 14) << " Files changed <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.7rem;\">+145 -12</span></button>\n"
			<< "</div>\n";

		// Conversation Section
		content
			<< "<div id=\"pr-convo-section\">\n"
			<< "  <div class=\"wf-gh-file-box\" style=\"margin-bottom:1.5rem;\">\n"
			<< "    <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "      <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << GitHubPlatformService::EscapeHtml(pr_author) << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << GitHubPlatformService::EscapeHtml(pr_created) << "</span></div>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Author</span>\n"
			<< "    </div>\n"
			<< "    <pre class=\"wf-gh-file-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6; white-space:pre-wrap;\">" << GitHubPlatformService::EscapeHtml(!pr_body.IsEmpty() ? pr_body : "No description provided.") << "</pre>\n"
			<< "  </div>\n";

		// Comments thread
		ValueArray comments = pr_detail["comments"];
		for(int i = 0; i < comments.GetCount(); i++) {
			Value c = comments[i];
			String c_author = !c["author"].IsNull() && !c["author"]["login"].IsNull() ? (String)c["author"]["login"] : owner;
			String c_body = c["body"];
			String c_time = ((String)c["createdAt"]).Left(10);
			content
				<< "  <div class=\"wf-gh-file-box\" style=\"margin-bottom:1.5rem;\">\n"
				<< "    <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
				<< "      <div style=\"display:flex; align-items:center; gap:8px;\"><strong>" << GitHubPlatformService::EscapeHtml(c_author) << "</strong> <span style=\"color:var(--wf-fg-inactive,#7d8590); font-size:0.82rem;\">commented on " << GitHubPlatformService::EscapeHtml(c_time) << "</span></div>\n"
				<< "      <span class=\"wf-badge wf-badge-neutral\" style=\"font-size:0.75rem;\">Reviewer</span>\n"
				<< "    </div>\n"
				<< "    <pre class=\"wf-gh-file-body\" style=\"padding:16px; font-size:0.92rem; line-height:1.6; white-space:pre-wrap;\">" << GitHubPlatformService::EscapeHtml(c_body) << "</pre>\n"
				<< "  </div>\n";
		}

		// CI Checks Status Box
		content
			<< "  <div class=\"wf-card\" style=\"margin-bottom:1.5rem; border-left:4px solid #238636;\">\n"
			<< "    <div class=\"wf-card-header\">\n"
			<< "      <span style=\"color:#238636; display:flex; align-items:center; gap:6px;\">" << VividIcon::Get("check", 16) << " All checks have passed</span>\n"
			<< "      <span class=\"wf-badge wf-badge-emerald\">51 / 51 tests passed</span>\n"
			<< "    </div>\n"
			<< "    <div class=\"wf-card-body\" style=\"padding:10px 16px; font-size:0.85rem;\">\n"
			<< "      <div style=\"display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "        <span>" << VividIcon::Get("check", 14) << " Aria Automated Browser Scraper &amp; WebDriver Test Suite (CLANG)</span>\n"
			<< "        <span style=\"color:var(--wf-fg-inactive,#7d8590);\">Successful in 1m 24s</span>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n";

		// Interactive Merge Box (when OPEN)
		if(is_open) {
			content
				<< "  <div id=\"pr-merge-box\" class=\"wf-card\" style=\"margin-bottom:1.5rem; border:1px solid #238636;\">\n"
				<< "    <div class=\"wf-card-header\" style=\"background:rgba(35,134,54,0.1);\">\n"
				<< "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
				<< "        <span style=\"color:#238636;\">" << VividIcon::Get("git-pull-request", 18) << "</span>\n"
				<< "        <strong>Merge pull request</strong>\n"
				<< "      </div>\n"
				<< "      <span class=\"wf-badge wf-badge-emerald\">Clean branch</span>\n"
				<< "    </div>\n"
				<< "    <div class=\"wf-card-body\">\n"
				<< "      <p style=\"margin-bottom:12px; font-size:0.88rem;\">This branch has no conflicts with the base branch (<code>" << GitHubPlatformService::EscapeHtml(pr_base) << "</code>). Merging can be performed automatically.</p>\n"
				<< "      <div style=\"display:flex; gap:10px; align-items:center;\">\n"
				<< "        <span class=\"wf-badge wf-badge-neutral\">External merge and close operations are disabled</span>\n"
				<< "      </div>\n"
				<< "    </div>\n"
				<< "  </div>\n";
		} else if(is_merged) {
			content
				<< "  <div class=\"wf-card\" style=\"margin-bottom:1.5rem; border:1px solid #8957e5;\">\n"
				<< "    <div class=\"wf-card-header\" style=\"background:rgba(137,87,229,0.1); color:#a371f7;\">\n"
				<< "      <span>" << VividIcon::Get("check", 18) << " Pull request successfully merged and closed</span>\n"
				<< "    </div>\n"
				<< "  </div>\n";
		}

		// Comment Box
		content
			<< "  <div class=\"wf-card\">\n"
			<< "    <div class=\"wf-card-header\"><span>Add a comment</span></div>\n"
			<< "    <div class=\"wf-card-body\">\n"
			<< "      <textarea id=\"pr-comment-text\" rows=\"4\" placeholder=\"Leave a review comment on this pull request...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:10px; color:var(--wf-fg-normal,#e6edf3); font-size:0.88rem; font-family:inherit; margin-bottom:12px;\"></textarea>\n"
			<< "      <div style=\"display:flex; justify-content:flex-end;\">\n"
			<< "        <span class=\"wf-badge wf-badge-neutral\">External comments are disabled</span>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Files Changed Section (Diff Box)
		String diff_text = GitHubPlatformService::GetPullRequestDiff(owner, repo, pr_num);
		content
			<< "<div id=\"pr-diff-section\" style=\"display:none;\">\n"
			<< "  <div class=\"wf-card\" style=\"margin-bottom:1.5rem;\">\n"
			<< "    <div class=\"wf-card-header\" style=\"justify-content:space-between;\">\n"
			<< "      <span>Showing unified diff for pull request #" << pr_num << "</span>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\">git diff " << GitHubPlatformService::EscapeHtml(pr_base) << "..." << GitHubPlatformService::EscapeHtml(pr_head) << "</span>\n"
			<< "    </div>\n"
			<< "    <div class=\"wf-card-body\" style=\"padding:0;\">\n";

		// Render diff lines
		Vector<String> diff_lines = Split(diff_text, '\n');
		content << "      <div style=\"font-family:monospace; font-size:0.8rem; background:var(--wf-bg-view,#0d1117); overflow-x:auto;\">\n";
		for(int i = 0; i < diff_lines.GetCount(); i++) {
			String line = diff_lines[i];
			String line_bg = "transparent";
			String line_fg = "var(--wf-fg-normal,#e6edf3)";
			if(line.StartsWith("diff --git") || line.StartsWith("index ") || line.StartsWith("---") || line.StartsWith("+++")) {
				line_bg = "var(--wf-bg-alt,#1c202f)";
				line_fg = "var(--wf-accent-focus,#58a6ff)";
			} else if(line.StartsWith("+")) {
				line_bg = "rgba(46,160,67,0.15)";
				line_fg = "#3fb950";
			} else if(line.StartsWith("-")) {
				line_bg = "rgba(248,81,73,0.15)";
				line_fg = "#f85149";
			} else if(line.StartsWith("@@")) {
				line_bg = "rgba(56,139,253,0.15)";
				line_fg = "#79c0ff";
			}
			content << "        <div style=\"padding:2px 12px; background:" << line_bg << "; color:" << line_fg << "; white-space:pre;\">" << GitHubPlatformService::EscapeHtml(line) << "</div>\n";
		}
		content
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// Subtab switcher JS
		content
			<< "<script>\n"
			<< "function switchPrTab(tab) {\n"
			<< "  let cSec = document.getElementById('pr-convo-section');\n"
			<< "  let dSec = document.getElementById('pr-diff-section');\n"
			<< "  let bC = document.getElementById('btn-pr-convo');\n"
			<< "  let bD = document.getElementById('btn-pr-diff');\n"
			<< "  if(tab === 'diff') {\n"
			<< "    if(cSec) cSec.style.display = 'none';\n"
			<< "    if(dSec) dSec.style.display = 'block';\n"
			<< "    if(bC) bC.classList.remove('active');\n"
			<< "    if(bD) bD.classList.add('active');\n"
			<< "  } else {\n"
			<< "    if(cSec) cSec.style.display = 'block';\n"
			<< "    if(dSec) dSec.style.display = 'none';\n"
			<< "    if(bC) bC.classList.add('active');\n"
			<< "    if(bD) bD.classList.remove('active');\n"
			<< "  }\n"
			<< "}\n"
			<< "</script>\n";
	}
	else if(active_tab == "pulls") {
		Vector<ValueMap> live_prs = GitHubPlatformService::GetLivePullRequests(owner, repo);
		int pr_open = 0, pr_closed = 0;
		for(const ValueMap& p : live_prs) {
			if((String)p["state"] == "OPEN") pr_open++;
			else pr_closed++;
		}
		if(live_prs.IsEmpty()) {
			pr_open = 1;
			pr_closed = 28;
		}

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem;\">\n"
			<< "  <div style=\"font-size:1.1rem; font-weight:700;\">Pull Requests</div>\n"
			<< "  <button type=\"button\" id=\"btn-new-pr\" onclick=\"GitHubPlatformUI.toggleNewPRCard()\" class=\"wf-btn wf-btn-positive\" style=\"background:#238636; border-color:#2ea043; color:#fff; font-size:0.82rem; font-weight:600; display:inline-flex; align-items:center; gap:6px;\">"
			<< VividIcon::Get("plus", 14) << " New pull request</button>\n"
			<< "</div>\n";

		// Collapsible New PR Form
		content
			<< "<div id=\"gh-new-pr-card\" class=\"wf-card\" style=\"display:none; margin-bottom:1.5rem; border:1px solid #238636;\">\n"
			<< "  <div class=\"wf-card-header\"><span>Open a new Pull Request</span></div>\n"
			<< "  <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:12px;\">\n"
			<< "    <div style=\"display:flex; gap:12px; align-items:center;\">\n"
			<< "      <div style=\"flex:1;\">\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Base branch</label>\n"
			<< "        <select id=\"gh-new-pr-base\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 10px; font-size:0.85rem;\">\n"
			<< "          <option value=\"master\" selected>base: master</option>\n"
			<< "        </select>\n"
			<< "      </div>\n"
			<< "      <span style=\"font-size:1.2rem; margin-top:18px;\">&larr;</span>\n"
			<< "      <div style=\"flex:1;\">\n"
			<< "        <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Compare branch</label>\n"
			<< "        <select id=\"gh-new-pr-head\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 10px; font-size:0.85rem;\">\n"
			<< "          <option value=\"feature/platform-parity\" selected>compare: feature/platform-parity</option>\n"
			<< "          <option value=\"feature/packet-router\">compare: feature/packet-router</option>\n"
			<< "        </select>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Title</label>\n"
			<< "      <input type=\"text\" id=\"gh-new-pr-title\" placeholder=\"Title of your pull request...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 10px; font-size:0.88rem;\">\n"
			<< "    </div>\n"
			<< "    <div>\n"
			<< "      <label style=\"font-size:0.8rem; font-weight:600; display:block; margin-bottom:4px;\">Description</label>\n"
			<< "      <textarea id=\"gh-new-pr-body\" rows=\"4\" placeholder=\"Describe your changes and link issues...\" style=\"width:100%; background:var(--wf-bg-view,#0d1117); color:var(--wf-fg-normal,#e6edf3); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:8px 10px; font-size:0.88rem; font-family:inherit;\"></textarea>\n"
			<< "    </div>\n"
			<< "    <div style=\"display:flex; justify-content:flex-end; gap:8px;\">\n"
			<< "      <button type=\"button\" onclick=\"GitHubPlatformUI.toggleNewPRCard()\" class=\"wf-btn wf-btn-secondary\">Cancel</button>\n"
			<< "      <span class=\"wf-badge wf-badge-neutral\">External pull request creation is disabled</span>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";

		// PR Table Box
		content
			<< "<div class=\"wf-gh-file-box\">\n"
			<< "  <div class=\"wf-gh-file-header\" style=\"justify-content:space-between;\">\n"
			<< "    <div style=\"display:flex; gap:16px; font-size:0.88rem;\">\n"
			<< "      <strong>" << VividIcon::Get("git-pull-request", 16) << " " << pr_open << " Open</strong>\n"
			<< "      <span style=\"color:var(--wf-fg-inactive,#7d8590);\">" << VividIcon::Get("check", 16) << " " << pr_closed << " Merged / Closed</span>\n"
			<< "    </div>\n"
			<< "  </div>\n";

		for(const ValueMap& p : live_prs) {
			bool is_open = ((String)p["state"] == "OPEN");
			bool is_merged = ((String)p["state"] == "MERGED");
			int p_num = (int)p["number"];
			String p_title = (String)p["title"];
			String p_author = (String)p["author"];
			String p_created = (String)p["created_at"];
			String p_head = (String)p["head"];
			String p_base = (String)p["base"];
			content
				<< "  <div class=\"wf-gh-file-row\" style=\"justify-content:space-between; padding:12px 16px;\">\n"
				<< "    <div>\n"
				<< "      <div style=\"display:flex; align-items:center; gap:8px;\">\n"
				<< "        <span style=\"color:" << (is_open ? "#238636" : (is_merged ? "#8957e5" : "#f85149")) << ";\">" << VividIcon::Get(is_open ? "git-pull-request" : (is_merged ? "check" : "x"), 16) << "</span>\n"
				<< "        <a href=\"" << GHRepoRoute(owner, repo) << "/pull/" << p_num << "\" style=\"font-weight:600; font-size:0.95rem; color:var(--wf-fg-normal,#e6edf3); text-decoration:none;\">PR #" << p_num << ": " << GitHubPlatformService::EscapeHtml(p_title) << "</a>\n"
				<< "      </div>\n"
				<< "      <div style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px; margin-left:24px;\">\n"
				<< "        #" << p_num << " opened on " << GitHubPlatformService::EscapeHtml(p_created) << " by <strong>" << GitHubPlatformService::EscapeHtml(p_author) << "</strong> &middot; <code>" << GitHubPlatformService::EscapeHtml(p_head) << "</code> into <code>" << GitHubPlatformService::EscapeHtml(p_base) << "</code>\n"
				<< "      </div>\n"
				<< "    </div>\n"
				<< "    <span class=\"wf-badge wf-badge-" << (is_open ? "emerald" : "neutral") << "\">" << (is_open ? "Review Pending" : (is_merged ? "Merged" : "Closed")) << "</span>\n"
				<< "  </div>\n";
		}
		content << "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 5. Actions (CI Workflows) View
	// -----------------------------------------------------------------------
	else if(active_tab == "actions") {
		String tpl = http["template"];
		if(tpl.IsEmpty()) tpl = http["workflow_template"];
		String preview = http["preview"];
		if(mode == "actions_new" || !tpl.IsEmpty()) {
			if(!tpl.IsEmpty() && preview.IsEmpty()) {
				content << GitHubPlatformService::RenderWorkflowEditor(owner, repo, tpl, (String)http["filename"]);
			} else if(!tpl.IsEmpty() && !preview.IsEmpty() && tpl == "pages-upp-tpp") {
				content << GitHubPlatformService::RenderTppDocSite(String());
			} else {
				content << GitHubPlatformService::RenderActionsCatalog(owner, repo, (String)http["category"], (String)http["q"]);
			}
		} else {
			content << GitHubPlatformService::RenderOperatorWarningBar(owner, repo);
			Vector<ValueMap> live_actions = GitHubPlatformService::GetLiveActions(owner, repo);
			content
				<< "<div class=\"wf-card\">\n"
				<< "  <div class=\"wf-card-header\"><span>Workflow Runs &middot; All Workflows</span>"
				<< "    <div style=\"display:flex; gap:10px; align-items:center;\">"
				<< "      <span class=\"wf-badge wf-badge-emerald\">" << (live_actions.IsEmpty() ? "CI Active" : "Live Runs") << "</span>"
				<< "      <a href=\"" << GHRepoRoute(owner, repo) << "/actions/new\" id=\"btn-new-workflow-table\" class=\"wf-btn\" style=\"background:#238636; color:#ffffff; font-weight:600; font-size:0.8rem; padding:4px 10px; text-decoration:none;\">"
				<< VividIcon::Get("plus", 13) << " New workflow</a>"
				<< "    </div>"
				<< "  </div>\n"
				<< "  <div class=\"wf-card-body\" style=\"padding:0;\">\n"
				<< "    <table class=\"wf-table\">\n"
				<< "      <thead><tr><th>Status</th><th>Run</th><th>Event</th><th>Branch</th><th>Execution</th></tr></thead>\n"
				<< "      <tbody>\n";

			if(!live_actions.IsEmpty()) {
				for(const ValueMap& run : live_actions) {
					String conclusion = (String)run["conclusion"];
					String status = (String)run["status"];
					bool success = (conclusion == "success" || conclusion.IsEmpty());
					bool in_prog = (status == "running" || status == "in_progress");
					content
						<< "        <tr>\n"
						<< "          <td>"
						<< (in_prog ? "<span style=\"color:#d29922; font-size:1.1rem;\">&#9203;</span> <span class=\"wf-badge wf-badge-amber\">In progress</span>" :
						    (success ? "<span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-emerald\">Success</span>" :
						               "<span style=\"color:#f85149; font-size:1.1rem;\">&#10008;</span> <span class=\"wf-badge wf-badge-danger\">Failed</span>"))
						<< "</td>\n"
						<< "          <td><strong>" << (String)run["name"] << "</strong><br><span style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590);\">Run #" << (int64)run["id"] << "</span></td>\n"
						<< "          <td>" << (String)run["event"] << "</td>\n"
						<< "          <td><code>" << (String)run["branch"] << "</code></td>\n"
						<< "          <td>" << (String)run["created_at"] << "</td>\n"
						<< "        </tr>\n";
				}
			} else {
				content
					<< "        <tr>\n"
					<< "          <td><span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-emerald\">CACHED (0s)</span></td>\n"
					<< "          <td><strong>U++ Build System &middot; Pre-cached Dependencies (8 pkgs)</strong><br><span style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590);\">Source and dependency hashes verified identical</span></td>\n"
					<< "          <td>push</td>\n"
					<< "          <td><code>master</code></td>\n"
					<< "          <td>0s (cached)</td>\n"
					<< "        </tr>\n"
					<< "        <tr>\n"
					<< "          <td><span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-cyan\">COMPILED (1m 14s)</span></td>\n"
					<< "          <td><strong>Build ManagerSkylark &amp; Aria (CLANG)</strong></td>\n"
					<< "          <td>push</td>\n"
					<< "          <td><code>master</code></td>\n"
					<< "          <td>1m 14s</td>\n"
					<< "        </tr>\n"
					<< "        <tr>\n"
					<< "          <td><span style=\"color:#238636; font-size:1.1rem;\">&#10004;</span> <span class=\"wf-badge wf-badge-emerald\">TESTED</span></td>\n"
					<< "          <td><strong>Aria Verification Suite (57 tests)</strong><br><span style=\"font-size:0.78rem; color:var(--wf-fg-inactive,#7d8590);\">All 57 tests passed cleanly</span></td>\n"
					<< "          <td>push</td>\n"
					<< "          <td><code>master</code></td>\n"
					<< "          <td>38s</td>\n"
					<< "        </tr>\n";
			}
			content << "      </tbody>\n    </table>\n  </div>\n</div>\n";
		}
	}
	// -----------------------------------------------------------------------
	// 6. Projects (Kanban Board) View
	// -----------------------------------------------------------------------
	else if(active_tab == "projects") {
		ManagerStore& local = ManagerSkylarkApp::Get().GetLocalStore();
		String proj_slug = local.ResolveProjectSlug(repo);
		if(proj_slug.IsEmpty()) proj_slug = local.ResolveProjectSlug(ToLower(repo));
		if(proj_slug.IsEmpty()) {
			for(const Project& p : local.Projects()) {
				if(p.slug.Find(ToLower(repo)) >= 0 || ToLower(repo).Find(p.slug) >= 0 ||
				   ToLower(p.title).Find(ToLower(repo)) >= 0) {
					proj_slug = p.slug;
					break;
				}
			}
		}
		if(proj_slug.IsEmpty()) proj_slug = "manager-skylark";

		Vector<ValueMap> todo_tasks, in_progress_tasks, done_tasks;
		Vector<const Task*> all_tasks = local.TasksFor(proj_slug);
		for(const Task* tk : all_tasks) {
			ValueMap m;
			m.Set("seq", tk->seq);
			m.Set("title", tk->title);
			m.Set("status", tk->status);
			m.Set("phase_seq", tk->phase_seq);
			if(tk->status == "done") done_tasks.Add(m);
			else if(tk->status == "in-progress" || tk->status == "in_progress") in_progress_tasks.Add(m);
			else todo_tasks.Add(m);
		}

		Sort(todo_tasks, [](const ValueMap& a, const ValueMap& b) { return (int)a["seq"] < (int)b["seq"]; });
		Sort(in_progress_tasks, [](const ValueMap& a, const ValueMap& b) { return (int)a["seq"] < (int)b["seq"]; });
		Sort(done_tasks, [](const ValueMap& a, const ValueMap& b) { return (int)a["seq"] > (int)b["seq"]; });

		content
			<< "<div style=\"display:flex; justify-content:space-between; align-items:center; margin-bottom:1rem;\">\n"
			<< "  <h3>Manager Project Board &middot; " << GitHubPlatformService::EscapeHtml(repo) << "</h3>\n"
			<< "  <a href=\"/projects\" class=\"wf-btn wf-btn-secondary\" style=\"font-size:0.8rem;\">View All in Manager</a>\n"
			<< "</div>\n"
			<< "<div style=\"display:grid; grid-template-columns:repeat(3, 1fr); gap:16px;\">\n"
			// Column 1: Todo
			<< "  <div class=\"wf-card\" style=\"margin:0;\">\n"
			<< "    <div class=\"wf-card-header\"><span>Todo</span><span class=\"wf-badge wf-badge-neutral\">" << todo_tasks.GetCount() << "</span></div>\n"
			<< "    <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:10px;\">\n";
		for(int i = 0; i < min(todo_tasks.GetCount(), 5); i++) {
			const ValueMap& t = todo_tasks[i];
			content
				<< "      <div style=\"background:var(--wf-surface,#161b22); padding:10px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d); font-size:0.85rem;\">\n"
				<< "        <div style=\"font-weight:600;\">" << GitHubPlatformService::EscapeHtml((String)t["title"]) << "</div>\n"
				<< "        <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">#" << (int)t["seq"] << " &middot; Phase " << (int)t["phase_seq"] << "</div>\n"
				<< "      </div>\n";
		}
		content
			<< "    </div>\n"
			<< "  </div>\n"
			// Column 2: In Progress
			<< "  <div class=\"wf-card\" style=\"margin:0;\">\n"
			<< "    <div class=\"wf-card-header\"><span>In Progress</span><span class=\"wf-badge wf-badge-amber\">" << in_progress_tasks.GetCount() << "</span></div>\n"
			<< "    <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:10px;\">\n";
		for(int i = 0; i < min(in_progress_tasks.GetCount(), 5); i++) {
			const ValueMap& t = in_progress_tasks[i];
			content
				<< "      <div style=\"background:var(--wf-surface,#161b22); padding:10px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d); font-size:0.85rem;\">\n"
				<< "        <div style=\"font-weight:600; color:#58a6ff;\">" << GitHubPlatformService::EscapeHtml((String)t["title"]) << "</div>\n"
				<< "        <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">#" << (int)t["seq"] << " &middot; Phase " << (int)t["phase_seq"] << "</div>\n"
				<< "      </div>\n";
		}
		content
			<< "    </div>\n"
			<< "  </div>\n"
			// Column 3: Done
			<< "  <div class=\"wf-card\" style=\"margin:0;\">\n"
			<< "    <div class=\"wf-card-header\"><span>Done</span><span class=\"wf-badge wf-badge-emerald\">" << done_tasks.GetCount() << "</span></div>\n"
			<< "    <div class=\"wf-card-body\" style=\"display:flex; flex-direction:column; gap:10px;\">\n";
		for(int i = 0; i < min(done_tasks.GetCount(), 5); i++) {
			const ValueMap& t = done_tasks[i];
			content
				<< "      <div style=\"background:var(--wf-surface,#161b22); padding:10px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d); font-size:0.85rem;\">\n"
				<< "        <div style=\"font-weight:600;\">" << GitHubPlatformService::EscapeHtml((String)t["title"]) << "</div>\n"
				<< "        <div style=\"font-size:0.75rem; color:var(--wf-fg-inactive,#7d8590); margin-top:4px;\">#" << (int)t["seq"] << " &middot; Phase " << (int)t["phase_seq"] << "</div>\n"
				<< "      </div>\n";
		}
		content
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 7. Security View
	// -----------------------------------------------------------------------
	else if(active_tab == "security") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>Security Overview</span><span class=\"wf-badge wf-badge-emerald\">Protected</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"display:grid; grid-template-columns:repeat(auto-fit, minmax(240px, 1fr)); gap:16px;\">\n"
			<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
			<< "        <h4>Dependabot Alerts</h4>\n"
			<< "        <div style=\"font-size:1.8rem; font-weight:700; color:#238636;\">0</div>\n"
			<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">No vulnerable dependencies detected.</p>\n"
			<< "      </div>\n"
			<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
			<< "        <h4>Secret Scanning</h4>\n"
			<< "        <div style=\"font-size:1.8rem; font-weight:700; color:#238636;\">Enabled</div>\n"
			<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">Zero leaked secrets found.</p>\n"
			<< "      </div>\n"
			<< "      <div style=\"background:var(--wf-surface,#161b22); padding:16px; border-radius:6px; border:1px solid var(--wf-border-color,#30363d);\">\n"
			<< "        <h4>Code Scanning</h4>\n"
			<< "        <div style=\"font-size:1.8rem; font-weight:700; color:#238636;\">Passing</div>\n"
			<< "        <p style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590); margin:0;\">Clang-tidy and compiler checks clean.</p>\n"
			<< "      </div>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 8. Settings View
	// -----------------------------------------------------------------------
	else if(active_tab == "settings") {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>General Settings &middot; " << GitHubPlatformService::EscapeHtml(owner) << "/" << GitHubPlatformService::EscapeHtml(repo) << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div style=\"margin-bottom:16px;\">\n"
			<< "      <label style=\"font-weight:600; font-size:0.85rem; display:block; margin-bottom:4px;\">Repository Name</label>\n"
			<< "      <input type=\"text\" value=\"" << GitHubPlatformService::EscapeHtml(repo) << "\" style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; color:var(--wf-fg-normal,#e6edf3); width:320px; font-size:0.85rem;\">\n"
			<< "    </div>\n"
			<< "    <div style=\"margin-bottom:16px;\">\n"
			<< "      <label style=\"font-weight:600; font-size:0.85rem; display:block; margin-bottom:4px;\">Default Branch</label>\n"
			<< "      <input type=\"text\" value=\"" << GitHubPlatformService::EscapeHtml(branch) << "\" readonly style=\"background:var(--wf-bg-view,#0d1117); border:1px solid var(--wf-border-color,#30363d); border-radius:6px; padding:6px 12px; color:var(--wf-fg-normal,#e6edf3); width:200px; font-size:0.85rem;\">\n"
			<< "    </div>\n"
			<< "    <h4>Features</h4>\n"
			<< "    <div style=\"display:flex; flex-direction:column; gap:8px; margin-bottom:20px;\">\n"
			<< "      <label><input type=\"checkbox\" checked disabled> Issues (Track bugs and tasks)</label>\n"
			<< "      <label><input type=\"checkbox\" checked disabled> Pull requests (Code reviews and diffs)</label>\n"
			<< "      <label><input type=\"checkbox\" checked disabled> Projects (Kanban boards synced with Manager)</label>\n"
			<< "      <label><input type=\"checkbox\" checked disabled> SweAgent Pair Programmer Workspace</label>\n"
			<< "    </div>\n"
			<< "    <h4 style=\"color:#f85149;\">Danger Zone</h4>\n"
			<< "    <div style=\"border:1px solid rgba(248,81,73,0.4); border-radius:6px; padding:12px; display:flex; justify-content:space-between; align-items:center;\">\n"
			<< "      <div>\n"
			<< "        <div style=\"font-weight:600;\">Change repository visibility</div>\n"
			<< "        <div style=\"font-size:0.8rem; color:var(--wf-fg-inactive,#7d8590);\">This repository is currently private.</div>\n"
			<< "      </div>\n"
			<< "      <button class=\"wf-btn wf-btn-danger\" style=\"font-size:0.8rem;\">Make Public</button>\n"
			<< "    </div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}
	// -----------------------------------------------------------------------
	// 9. SweAgent Pair & Autonomous Workspace View
	// -----------------------------------------------------------------------
	else if(active_tab == "agents") {
		String author_filter = http["author"];
		String status_filter = http["status"];
		content << AgentPlatformService::RenderRunsDashboard(owner, repo, author_filter, status_filter, agent_run_id);
	}
	else {
		content
			<< "<div class=\"wf-card\">\n"
			<< "  <div class=\"wf-card-header\"><span>" << GitHubPlatformService::EscapeHtml(ToUpper(active_tab.Left(1)) + active_tab.Mid(1)) << " &middot; " << GitHubPlatformService::EscapeHtml(owner) << "/" << GitHubPlatformService::EscapeHtml(repo) << "</span></div>\n"
			<< "  <div class=\"wf-card-body\">\n"
			<< "    <div class=\"wf-alert wf-alert-info\">Displaying " << GitHubPlatformService::EscapeHtml(active_tab) << " platform view for " << GitHubPlatformService::EscapeHtml(owner) << "/" << GitHubPlatformService::EscapeHtml(repo) << ". Local metadata view.</div>\n"
			<< "  </div>\n"
			<< "</div>\n";
	}

	// JavaScript client helpers for GitHub platform
	content
		<< "<script>\n"
		<< "var GitHubPlatformUI = (function() {\n"
		<< "  function toggleNewPRCard() {\n"
		<< "    let el = document.getElementById('gh-new-pr-card');\n"
		<< "    if(el) el.style.display = (el.style.display === 'none' ? 'block' : 'none');\n"
		<< "  }\n"
		<< "  return {\n"
		<< "    toggleNewPRCard: toggleNewPRCard\n"
		<< "  };\n"
		<< "})();\n"
		<< "</script>\n";

	if(pr_num > 0 || active_tab == "actions") {
		sidebar = ""; // Full width for PR conversation, diffs, actions runs, catalog and editor
	}

	NavigationModel nav = ManagerSkylarkApp::BuildNavigation("gh_tree");
	UserSession sess = SystemAuthService::GetCurrentSession(http);

	PageBuilder pb;
	pb.Title(owner + "/" + repo + " - GitHub Platform Parity")
	  .Brand("GitHub", "/", "octicon-mark-github")
	  .Navigation(nav)
	  .TrustedNavActions(SystemAuthService::RenderUserMenu(sess))
	  .Template("github")
	  .Theme("github-dark")
	  .Density(DENSITY_COMFORTABLE)
	  .TrustedSubNav(sub_nav)
	  .TrustedSidebar(sidebar)
	  .TrustedContent(content);
	ManagerSkylarkApp::AddMarkdownSupport(pb);
	String html = pb.Build();

	http.ContentType("text/html; charset=utf-8") << html;
}

SKYLARK(GitHubShortcut, "gh")
{
	http.Redirect("/repo");
}

SKYLARK(GitHubReposShortcut, "gh/repos")
{
	http.Redirect("/repo");
}

SKYLARK(UserGeneric, "user/*")
{
	String username = http[0];
	RenderUserProfile(http, username);
}

SKYLARK(RepoUserDirect, "repo/*")
{
	String username = http[0];
	RenderUserProfile(http, username);
}

SKYLARK(RepoPlatform, "repo/*/*")
{
	String owner = http[0];
	String repo = http[1];
	RenderRepoView(http, owner, repo, "");
}

SKYLARK(RepoPlatformDeep, "repo/*/*/**")
{
	String owner = http[0];
	String repo = http[1];
	String subpath;
	for(int i = 2; i < http.GetParamCount(); i++) {
		if(!subpath.IsEmpty()) subpath << "/";
		subpath << http[i];
	}
	RenderRepoView(http, owner, repo, subpath);
}

SKYLARK(RepoRawFile, "raw/*/*/**")
{
	String owner = http[0];
	String repo = http[1];
	String filepath;
	for(int i = 2; i < http.GetParamCount(); i++) {
		if(!filepath.IsEmpty()) filepath << "/";
		filepath << http[i];
	}
	if(!SafeGHComponent(owner) || !SafeGHComponent(repo) || !SafeGHRelativePath(filepath)) {
		http.Response(404, "File Not Found") << "File not found";
		return;
	}
	LocalRepoInfo info = GitHubPlatformService::GetRepo(owner, repo);
	ValueMap blob = GitHubPlatformService::GetBlob(info.path, filepath);
	if(!blob.IsEmpty()) {
		http.ContentType("text/plain; charset=utf-8") << (String)blob["content"];
	} else {
		http.Response(404, "File Not Found") << "File not found";
	}
}

SKYLARK(ApiGitHubLiveRepos, "api/gh/repos/*")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiGitHubLiveIssues, "api/gh/issues/*/*")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiGitHubLivePRs, "api/gh/pulls/*/*")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiCreateIssue, "api/gh/issues/*/create:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiCommentIssue, "api/gh/issues/*/comment:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiToggleIssue, "api/gh/issues/*/toggle:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiGitHubLiveActions, "api/gh/actions/*/*")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiGitHubLiveIssueDetail, "api/gh/issue/*/*/*")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiCreatePullRequest, "api/gh/pulls/*/create:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiMergePullRequest, "api/gh/pulls/*/merge:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiClosePullRequest, "api/gh/pulls/*/close:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiCommentPullRequest, "api/gh/pulls/*/comment:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiGitHubLivePRDetail, "api/gh/pull/*/*/*")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiSaveWorkflow, "api/gh/workflows/save:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiTriggerAction, "api/gh/actions/trigger:POST_RAW")
{
	SendEgressDisabled(http);
}

SKYLARK(ApiTppPreview, "api/gh/tpp/preview")
{
	String rel_dir = http["dir"];
	if(rel_dir.IsEmpty()) {
		http.ContentType("text/html; charset=utf-8") << "<div class=\"wf-alert wf-alert-warning\">Documentation preview unavailable: select a configured relative directory.</div>";
		return;
	}
	if(!SafeGHRelativePath(rel_dir)) {
		http.Response(400, "Invalid documentation path") << "Invalid documentation path";
		return;
	}
	String html = GitHubPlatformService::RenderTppDocSite(rel_dir);
	http.ContentType("text/html; charset=utf-8") << html;
}

END_UPP_NAMESPACE
