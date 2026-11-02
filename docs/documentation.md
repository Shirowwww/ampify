---
title: Documentation
---
{% capture readme %}{% include_relative README.md %}{% endcapture %}
{% assign repo = "https://github.com/Shirowwww/ampify/blob/main/" %}
{{ readme | replace: ".md)", ".html)" | replace: ".md#", ".html#"
  | replace: "../CONTRIBUTING.html", "CONTRIBUTING.md" | replace: "../CHANGELOG.html", "CHANGELOG.md"
  | replace: "../CREDITS.html", "CREDITS.md" | replace: "../knowledge/README.html", "knowledge/README.md"
  | replace: "](CONTRIBUTING.md", "](REPO/CONTRIBUTING.md" | replace: "](CHANGELOG.md", "](REPO/CHANGELOG.md"
  | replace: "](CREDITS.md", "](REPO/CREDITS.md" | replace: "](knowledge/README.md", "](REPO/knowledge/README.md"
  | replace: "REPO/", repo }}
