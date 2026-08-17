/* Login page. Standalone on purpose: it is the only page served before there
 * is a session, so it must not depend on api.js/state.js or anything else that
 * assumes one. */
(() => {
  const form = document.getElementById("loginForm");
  const errorBox = document.getElementById("loginError");
  const button = document.getElementById("loginBtn");

  function showError(message) {
    errorBox.textContent = message;
    errorBox.style.display = "block";
  }

  /* Where to go after signing in. ReturnUrl comes from the redirect the server
   * issued, and is accepted ONLY when it is a path on this same site - an
   * absolute URL there would turn the login page into an open redirect that
   * bounces users to an attacker's copy of it. */
  function safeReturnUrl() {
    const raw = new URLSearchParams(location.search).get("ReturnUrl");
    if (!raw) return "/";
    if (!raw.startsWith("/") || raw.startsWith("//")) return "/";
    return raw;
  }

  form.addEventListener("submit", async (e) => {
    e.preventDefault();
    errorBox.style.display = "none";
    button.disabled = true;
    button.textContent = "Signing in…";

    try {
      const response = await fetch("/api/auth/login", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        credentials: "same-origin",
        body: JSON.stringify({
          username: document.getElementById("username").value,
          password: document.getElementById("password").value
        })
      });

      if (!response.ok) {
        const body = await response.json().catch(() => ({}));
        showError(body.error || "Sign-in failed");
        return;
      }

      const me = await response.json();
      // A password an administrator typed in for them is not a password they
      // have chosen, so the console is not reachable until it is replaced.
      location.href = me.mustChangePassword ? "/?changePassword=1" : safeReturnUrl();
    } catch (err) {
      showError("Cannot reach the server");
    } finally {
      button.disabled = false;
      button.textContent = "Sign in";
    }
  });
})();
