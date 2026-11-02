---
title: Documentation
---
{% capture readme %}{% include_relative README.md %}{% endcapture %}
{{ readme | replace: ".md)", ".html)" | replace: ".md#", ".html#" }}
