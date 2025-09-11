var links;

function togglePathView() {
  for (var i = 0, len = links.length; i < len; i++) {
    links[i].classList.toggle("display-none");
  }
}

document.addEventListener('DOMContentLoaded', function() {
  links = document.getElementsByClassName("file-link");
  document.querySelector('.path-toggle').checked = false;
}, false);