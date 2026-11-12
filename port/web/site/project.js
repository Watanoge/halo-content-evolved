'use strict';

(() => {
  const androidBrowser = /android/i.test(navigator.userAgent);
  const nonDesktopDevice = /android|iphone|ipad|ipod|mobile/i.test(navigator.userAgent) ||
    window.matchMedia('(pointer: coarse)').matches;
  document.body.classList.toggle('android-device', androidBrowser);
  document.body.classList.toggle('non-desktop-device', nonDesktopDevice);

  const REPOSITORY = 'Watanoge/halo-content-evolved';
  const RELEASES_PAGE = `https://github.com/${REPOSITORY}/releases`;
  const RELEASES_API = `https://api.github.com/repos/${REPOSITORY}/releases?per_page=100`;
  const releaseUrl = (tag) => `${RELEASES_PAGE}/tag/${encodeURIComponent(tag)}`;
  const WATANOGE = { name: 'Watanoge', href: 'https://github.com/Watanoge' };
  const MOTALEV = { name: 'MotaLev', href: 'https://github.com/SutlabSol' };
  const CONTENT_EXTENDED = { label: 'Content Extended Additions', href: 'https://github.com/SutlabSol/Content-Extended-Additions' };
  const showcase = [
    {
      kind: 'Main mode',
      title: 'Firefight',
      images: [
        { src: 'features/firefight/firefight.webp', alt: 'Firefight on Blood Gulch, with enemy vehicle spawns placed in Forge' },
        { src: 'features/firefight/firefight_variants.webp', alt: 'The eight base Firefight variants, ready to edit or copy' },
        { src: 'features/firefight/firefight_no_remorse.webp', alt: 'Flood survival Firefight on the Custom Edition map No Remorse' },
        { src: 'features/firefight/firefight_house2.webp', alt: 'Firefight on the Custom Edition map House2, played as-is' },
      ],
      description: 'Survive waves or defend generators on any multiplayer map, with 8 base variants to play, edit, or build on.',
    },
    {
      kind: 'Content creation',
      title: 'Forge',
      images: [
        { src: 'features/forge/forge.webp', alt: 'A Forge map with placed vehicles, scenery, and AI' },
        { src: 'features/forge/forge_palette.webp', alt: 'The Forge palette with vehicles from a tag profile' },
        { src: 'features/forge/forge_actors_1.webp', alt: 'Campaign actors in the Forge palette, split by category' },
        { src: 'features/forge/forge_actors_2.webp', alt: 'Placing a campaign actor on a multiplayer map' },
        { src: 'features/forge/forge_actor_settings.webp', alt: 'A placed unit\'s settings: weapon, team, count, respawn, gamemodes, and spawn effect' },
        { src: 'features/forge/forge_firefight_spawns.webp', alt: 'Firefight infantry and vehicle spawns placed in Forge' },
        { src: 'features/forge/forge_dropship_route.webp', alt: 'A Firefight dropship zone with its flight route' },
        { src: 'features/forge/forge_dropship_flight.webp', alt: 'A dropship flying its Forge route' },
        { src: 'features/forge/forge_generators.webp', alt: 'Generator spawn points in the Forge objectives tab' },
      ],
      description: 'Build alone or with friends: place weapons, vehicles, scenery, doors, and campaign AI on any map, then save it as a regular Custom Edition map.',
    },
    {
      kind: 'Campaign',
      title: 'Campaign randomizer',
      images: [
        { src: 'features/randomizer/randomizer.webp', alt: 'Randomizer settings' },
        { src: 'features/randomizer/randomizer_gameplay.webp', alt: 'Campaign gameplay with randomized enemies and weapons' },
      ],
      description: 'Remix enemy squads, enemy weapons, and map pickups to create a fresh campaign run.',
    },
    {
      kind: 'Campaign',
      title: 'Campaign skulls',
      images: [
        { src: 'features/skulls/skulls.webp', alt: 'Campaign skull selection' },
        { src: 'features/skulls/skull-in-world.webp', alt: 'A skull placed in a campaign level' },
        { src: 'features/skulls/skull-dropped.webp', alt: 'A skull dropped during campaign gameplay' },
      ],
      description: 'Choose from 40 skulls, unlock them through play, or roll a random set for a tougher run.',
    },
    {
      kind: 'Campaign',
      title: 'Scoring and medals',
      images: [
        { src: 'features/scoring-medals/scoring.webp', alt: 'Scoring and medals settings' },
        { src: 'features/scoring-medals/scoring-campaign-overview.webp', alt: 'Campaign score and medal summary' },
        { src: 'features/scoring-medals/scoring-medals-multiplayer.webp', alt: 'Multiplayer score panel and medals' },
      ],
      description: 'Earn medals, chase level scores and times, and share campaign or multiplayer results on leaderboards.',
    },
    {
      kind: 'Campaign',
      title: 'Daily challenges',
      images: [{ src: 'features/campaign/daily-challenge.webp', alt: 'Daily campaign challenge with its level, skulls, and leaderboard' }],
      description: 'A shared daily setup gives everyone the same level, difficulty, randomizer seed, and skulls.',
    },
    {
      kind: 'Custom Edition',
      title: 'Tag Injector',
      images: [
        { src: 'features/tag-injection/tag-injector.webp', alt: 'Custom Edition weapon and enemy tags in a Halo campaign' },
        { src: 'features/tag-injection/tag_injection_multiplayer.webp', alt: 'Multiplayer tag profile selection' },
        { src: 'features/tag-injection/tag_injection_coop.webp', alt: 'Co-op campaign tag profile settings' },
      ],
      description: 'Bring weapons, enemies, and equipment from Custom Edition maps into solo, co-op, and multiplayer profiles.',
    },
    {
      kind: 'Content creation',
      title: 'Firing range',
      images: [
        { src: 'features/firing-range/firing-range.webp', alt: 'Firing range for testing tag injection profiles' },
        { src: 'features/firing-range/firing_range_guns.webp', alt: 'Custom weapons arranged in the firing range' },
        { src: 'features/firing-range/firing_range_ai.webp', alt: 'Custom AI arranged for testing' },
        { src: 'features/firing-range/firing_range_editmode.webp', alt: 'Firing range edit mode' },
      ],
      description: 'Test profile weapons, AI, animations, and balance before taking them into a match.',
    },
    {
      kind: 'Gamemodes',
      title: 'Floodfection and Tag Gun Game',
      images: [
        { src: 'features/custom-gamemodes/gamemodes.webp', alt: 'Custom tag injection gamemodes' },
        { src: 'features/custom-gamemodes/floodfection_gameplay.webp', alt: 'Floodfection gameplay' },
        { src: 'features/custom-gamemodes/floodfection_gameplay_flood.webp', alt: 'Flood forms in Floodfection' },
        { src: 'features/custom-gamemodes/tag_gungame_gameplay.webp', alt: 'Tag Gun Game using custom weapons' },
      ],
      description: 'Custom multiplayer modes use tag profiles as the foundation for new Halo CE experiences.',
    },
    {
      kind: 'Gameplay upgrade',
      title: 'Third-person mode',
      images: [
        { src: 'features/third-person/third_person.webp', alt: 'Third-person campaign gameplay' },
        { src: 'features/third-person/third_person_settings.webp', alt: 'Third-person camera settings' },
      ],
      description: 'Play on foot with an adjustable over-the-shoulder view in campaign, co-op, and supported modes.',
    },
    {
      kind: 'Social',
      title: 'Friends, chat, and joining',
      images: [
        { src: 'features/social-features/social-menu.webp', alt: 'Halo Content Evolved social menu' },
        { src: 'features/social-features/main_menu_socials.webp', alt: 'Social profile and friends panel' },
        { src: 'features/social-features/socials_requests.webp', alt: 'Friend request interface' },
        { src: 'features/social-features/socials_player_records.webp', alt: 'Player records and match history' },
        { src: 'features/social-features/socials_user_tooltip.webp', alt: 'Player quick actions' },
        { src: 'features/social-features/socials_join_from_list.webp', alt: 'Joining a friend from the friends list' },
      ],
      description: 'Find friends, talk in global and match chat, and join friends in-game.',
    },
    {
      kind: 'Social',
      title: 'Ranks and emblems',
      images: [
        { src: 'features/ranks-emblems/player_card.webp', alt: 'A player card with rank, XP bar, emblem, and career stats' },
        { src: 'features/ranks-emblems/player_card_firefight.webp', alt: 'A player card with Firefight career stats' },
        { src: 'features/ranks-emblems/friends_list_ranks.webp', alt: 'Rank and emblem icons beside each friend in the friends list' },
        { src: 'features/ranks-emblems/rank_progress.webp', alt: 'Rank progress after a match, with the XP earned' },
      ],
      description: 'Earn XP for every finished match outside Forge, climb the ranks, and pick your own emblem. Ranks are cosmetic only.',
    },
    {
      kind: 'Social',
      title: 'Creator profile',
      images: [{ src: 'features/creator-profile/creator-profile.webp', alt: 'A creator profile with Forge time, maps made, and tag profile counts' }],
      description: 'See how long a player has spent creating and how many Forge maps and tag profiles they have made. File share will live here.',
    },
  ];

  const roadmap = {
    planned: [
      { kind: 'Gamemode', title: 'UNSC vs Covenant' },
      { kind: 'Content creation', title: 'Tag profile workshop' },
      { kind: 'Gameplay upgrade', title: 'Multiplayer skulls' },
      { kind: 'Gameplay upgrade', title: 'Custom single-player upgrades', description: 'Skulls, the randomizer, and tag injection on custom single-player maps.' },
      { kind: 'Gamemode', title: 'Floodfection V3' },
    ],
    doing: [
      { kind: 'Social feature', title: 'File share', description: 'Download the Forge maps and tag profiles a player has made from their creator profile.' },
    ],
    done: [
      { kind: 'Quality of life', title: 'Map sub-folders', release: 'switch-fixes-01', authors: [WATANOGE] },
      { kind: 'Main mode', title: 'Daily challenge', release: 'ultimate-campaign-v0.1', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Campaign skulls', release: 'ultimate-campaign-v0.1', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Campaign randomizer', release: 'ultimate-campaign-v0.1', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Campaign scoring and medals', release: 'ultimate-campaign-v0.1', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Campaign leaderboards', release: 'ultimate-campaign-v0.1', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Solo campaign tag injection', release: 'build-105', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Multiplayer scoring and medals', release: 'build-107', authors: [WATANOGE] },
      {
        kind: 'Gameplay upgrade',
        title: 'Multiplayer bots',
        release: 'build-112',
        authors: [
          WATANOGE,
          { name: 'Thelinkin3000', href: 'https://github.com/thelinkin3000' },
        ],
      },
      { kind: 'Gameplay upgrade', title: 'Multiplayer tag injection', release: 'build-112', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Co-op campaign tag injection', release: 'build-113', authors: [WATANOGE] },
      { kind: 'Content creation', title: 'Firing range for tag profiles', release: 'build-113', authors: [WATANOGE] },
      {
        kind: 'Main mode',
        title: 'Theater mode',
        release: 'build-113',
        authors: [{ name: 'Thelinkin3000', href: 'https://github.com/thelinkin3000' }],
      },
      { kind: 'Content creation', title: 'Tag injection gamemodes', release: 'build-115', authors: [WATANOGE] },
      { kind: 'Gamemode', title: 'Floodfection V1', release: 'build-115', authors: [WATANOGE] },
      { kind: 'Gamemode', title: 'Tag gun game', release: 'build-115', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Third-person mode', release: 'build-117', authors: [WATANOGE] },
      { kind: 'Gamemode', title: 'Floodfection V2', release: 'build-117', authors: [WATANOGE] },
      { kind: 'Social feature', title: 'Friend list', release: 'build-121', authors: [WATANOGE] },
      { kind: 'Social feature', title: 'Global and match chat', release: 'build-121', authors: [WATANOGE] },
      { kind: 'Social feature', title: 'Join friends in-game', release: 'build-121', authors: [WATANOGE] },
      { kind: 'Feature', title: 'Cross-platform account sync', release: 'build-123', authors: [WATANOGE] },
      {
        kind: 'Main mode',
        title: 'Forge',
        description: 'Solo and multiplayer map building with tag profiles. Saved maps are regular Custom Edition maps.',
        source: CONTENT_EXTENDED,
        release: 'build-127',
        authors: [WATANOGE, MOTALEV],
      },
      { kind: 'Content creation', title: 'Forge AI actors', description: 'Campaign actors on any multiplayer map, with weapon, team, respawn, and gamemode settings.', release: 'build-127', authors: [WATANOGE] },
      {
        kind: 'Main mode',
        title: 'Firefight',
        description: 'Survive and Generator Defense on every multiplayer map.',
        source: CONTENT_EXTENDED,
        release: 'build-127',
        authors: [WATANOGE, MOTALEV],
      },
      { kind: 'Gamemode', title: 'Firefight variants', description: '8 base variants, plus your own.', release: 'build-127', authors: [WATANOGE] },
      { kind: 'Content creation', title: 'Forge Firefight spawns', description: 'Enemy infantry and vehicle spawns, dropship routes, and generator points.', release: 'build-127', authors: [WATANOGE] },
      { kind: 'Social feature', title: 'Ranks and emblems', release: 'build-127', authors: [WATANOGE] },
      { kind: 'Gameplay upgrade', title: 'Firefight enemy loadouts', release: 'build-128', authors: [WATANOGE] },
      { kind: 'Social feature', title: 'Creator profile', release: 'build-129', authors: [WATANOGE] },
    ],
  };

  function renderRoadmap() {
    for (const [lane, features] of Object.entries(roadmap)) {
      const list = document.getElementById(`roadmap-${lane}`);
      document.getElementById(`${lane}-count`).textContent = String(features.length).padStart(2, '0');
      list.replaceChildren(...features.map((feature) => {
        const card = document.createElement('article');
        card.className = 'feature-card';
        const kind = document.createElement('p');
        kind.className = 'feature-kind';
        kind.textContent = feature.kind;
        const heading = document.createElement('h4');
        heading.textContent = feature.title;
        card.append(kind, heading);
        if (feature.description) {
          const description = document.createElement('p');
          description.className = 'feature-description';
          description.textContent = feature.description;
          card.append(description);
        }
        if (feature.source) {
          const source = document.createElement('a');
          source.className = 'feature-source';
          source.href = feature.source.href;
          source.target = '_blank';
          source.rel = 'noopener';
          source.textContent = `Based on ${feature.source.label}`;
          card.append(source);
        }
        const footer = document.createElement('div');
        footer.className = 'feature-footer';
        if (feature.release) {
          const release = document.createElement('a');
          release.className = 'feature-release';
          release.href = releaseUrl(feature.release);
          release.target = '_blank';
          release.rel = 'noopener';
          release.textContent = feature.release;
          footer.append(release);
        }
        if (feature.authors && feature.authors.length) {
          const authors = document.createElement('span');
          authors.className = 'feature-authors';
          authors.append(document.createTextNode('By '));
          feature.authors.forEach((author, index) => {
            if (index) authors.append(document.createTextNode(' + '));
            const link = document.createElement('a');
            link.href = author.href;
            link.target = '_blank';
            link.rel = 'noopener';
            link.textContent = author.name;
            authors.append(link);
          });
          footer.append(authors);
        }
        if (footer.childNodes.length) card.append(footer);
        return card;
      }));
    }
  }

  function renderFeatures() {
    const list = document.getElementById('feature-list');
    list.replaceChildren(...showcase.map((feature) => {
      const card = document.createElement('article');
      card.className = 'showcase-card';
      const collage = document.createElement('button');
      collage.className = 'showcase-collage';
      collage.type = 'button';
      collage.dataset.count = String(feature.images.length);
      collage.setAttribute('aria-label', `View ${feature.images.length} ${feature.images.length === 1 ? 'image' : 'images'} for ${feature.title}`);
      collage.title = `View images: ${feature.title}`;
      const columns = feature.images.length < 3 ? feature.images.length : feature.images.length <= 4 ? 2 : 3;
      collage.style.gridTemplateColumns = `repeat(${columns}, minmax(0, 1fr))`;
      collage.style.gridTemplateRows = `repeat(${Math.ceil(feature.images.length / columns)}, minmax(0, 1fr))`;
      feature.images.forEach((source, index) => {
        const image = document.createElement('img');
        image.src = source.src;
        image.alt = '';
        image.loading = 'lazy';
        image.dataset.imageIndex = String(index);
        collage.append(image);
      });
      collage.addEventListener('click', (event) => {
        const image = event.target.closest('img[data-image-index]');
        openFeatureCarousel(feature, image ? Number(image.dataset.imageIndex) : 0);
      });
      const body = document.createElement('div');
      body.className = 'showcase-copy';
      const kind = document.createElement('p');
      kind.className = 'feature-kind';
      kind.textContent = feature.kind;
      const title = document.createElement('h3');
      title.textContent = feature.title;
      const description = document.createElement('p');
      description.textContent = feature.description;
      body.append(kind, title, description);
      card.append(collage, body);
      return card;
    }));
  }

  let activeFeature = null;
  let activeImageIndex = 0;

  function renderFeatureCarousel() {
    if (!activeFeature) return;
    const image = activeFeature.images[activeImageIndex];
    document.getElementById('feature-carousel-kind').textContent = activeFeature.kind;
    document.getElementById('feature-carousel-title').textContent = activeFeature.title;
    const preview = document.getElementById('feature-carousel-image');
    preview.src = image.src;
    preview.alt = image.alt;
    document.getElementById('feature-carousel-caption').textContent = image.alt;
    document.getElementById('feature-carousel-count').textContent = `${activeImageIndex + 1} / ${activeFeature.images.length}`;
    document.getElementById('feature-carousel-thumbnails').replaceChildren(...activeFeature.images.map((item, index) => {
      const thumbnail = document.createElement('button');
      thumbnail.type = 'button';
      thumbnail.className = 'carousel-thumbnail';
      thumbnail.setAttribute('aria-label', `Show image ${index + 1}: ${item.alt}`);
      thumbnail.setAttribute('aria-current', String(index === activeImageIndex));
      const thumbImage = document.createElement('img');
      thumbImage.src = item.src;
      thumbImage.alt = '';
      thumbImage.loading = 'lazy';
      thumbnail.append(thumbImage);
      thumbnail.addEventListener('click', () => {
        activeImageIndex = index;
        renderFeatureCarousel();
      });
      return thumbnail;
    }));
  }

  function openFeatureCarousel(feature, index) {
    activeFeature = feature;
    activeImageIndex = index;
    renderFeatureCarousel();
    document.getElementById('feature-carousel').showModal();
  }

  function moveFeatureCarousel(direction) {
    if (!activeFeature) return;
    const count = activeFeature.images.length;
    activeImageIndex = (activeImageIndex + direction + count) % count;
    renderFeatureCarousel();
  }

  function releaseDate(value) {
    const date = new Date(value);
    return Number.isNaN(date.valueOf()) ? '' : new Intl.DateTimeFormat('en', { dateStyle: 'medium' }).format(date);
  }

  function assetLabel(name) {
    const format = /\.apk$/i.test(name) ? ' APK' : /\.zip$/i.test(name) ? ' ZIP' : '';
    const normalized = name.replace(/^halo-/, '').replace(/\.zip$|\.apk$/i, '');
    return normalized.split('-').map((word) => word[0].toUpperCase() + word.slice(1)).join(' ') + format;
  }

  const platformOptions = [
    { key: 'android', label: 'Android APK', matches: (name) => /android/i.test(name) && /\.apk$/i.test(name) && !/debug/i.test(name) },
    { key: 'switch', label: 'Nintendo Switch', matches: (name) => /switch/i.test(name) && /\.zip$/i.test(name) && !/debug/i.test(name) },
    { key: 'web', label: 'Web', matches: (name) => /web/i.test(name) && /\.zip$/i.test(name) && !/debug/i.test(name) },
    { key: 'windows', label: 'Windows', matches: (name) => /windows/i.test(name) && /\.zip$/i.test(name) && !/debug/i.test(name) },
  ];

  function validReleaseAsset(asset) {
    try {
      const url = new URL(asset.browser_download_url);
      return url.protocol === 'https:' && url.hostname === 'github.com' &&
        url.pathname.startsWith(`/Watanoge/halo-content-evolved/releases/download/`);
    } catch {
      return false;
    }
  }

  function showDownloadOptions(release) {
    const modal = document.getElementById('download-modal');
    const title = document.getElementById('download-modal-title');
    const detail = document.getElementById('download-modal-release');
    const options = document.getElementById('download-modal-options');
    title.textContent = 'Download';
    detail.textContent = `${release.name || 'Halo Content Evolved'} · ${release.tag_name || ''}`;
    const assets = Array.isArray(release.assets) ? release.assets.filter((asset) => asset &&
      typeof asset.name === 'string' && typeof asset.browser_download_url === 'string' && validReleaseAsset(asset)) : [];
    const links = platformOptions.flatMap((platform) => {
      const asset = assets.find((candidate) => platform.matches(candidate.name));
      if (!asset) return [];
      const link = document.createElement('a');
      link.className = 'platform-download';
      link.href = asset.browser_download_url;
      link.target = '_blank';
      link.rel = 'noopener';
      const label = document.createElement('span');
      label.textContent = platform.label;
      const size = document.createElement('span');
      size.textContent = `${(asset.size / 1048576).toFixed(1)} MB`;
      link.append(label, size);
      return [link];
    });
    if (!links.length) {
      const message = document.createElement('p');
      message.className = 'release-empty';
      message.textContent = 'No release downloads are available for this build.';
      options.replaceChildren(message);
    } else {
      options.replaceChildren(...links);
    }
    modal.showModal();
  }

  function appendInlineMarkdown(parent, text) {
    const pattern = /\[([^\]]+)\]\((https?:\/\/[^\s)]+)\)|\*\*([^*]+)\*\*|`([^`]+)`/g;
    let cursor = 0;
    for (const match of text.matchAll(pattern)) {
      parent.append(document.createTextNode(text.slice(cursor, match.index)));
      if (match[1]) {
        const link = document.createElement('a');
        link.href = match[2];
        link.target = '_blank';
        link.rel = 'noopener';
        link.textContent = match[1];
        parent.append(link);
      } else if (match[3]) {
        const strong = document.createElement('strong');
        strong.textContent = match[3];
        parent.append(strong);
      } else {
        const code = document.createElement('code');
        code.textContent = match[4];
        parent.append(code);
      }
      cursor = match.index + match[0].length;
    }
    parent.append(document.createTextNode(text.slice(cursor)));
  }

  function renderMarkdown(markdown) {
    const container = document.createElement('div');
    container.className = 'release-markdown';
    const lines = markdown.replace(/<img\b[\s\S]*?\/?>/gi, '').split(/\r?\n/);
    let index = 0;
    while (index < lines.length) {
      const line = lines[index].trim();
      if (!line || /^<\/?[a-z][^>]*>$/i.test(line)) {
        index++;
        continue;
      }
      const heading = /^(#{1,4})\s+(.+)$/.exec(line);
      if (heading) {
        const element = document.createElement(heading[1].length < 3 ? 'h3' : 'h4');
        appendInlineMarkdown(element, heading[2]);
        container.append(element);
        index++;
        continue;
      }
      if (/^[-*]\s+/.test(line)) {
        const list = document.createElement('ul');
        while (index < lines.length && /^\s*[-*]\s+/.test(lines[index])) {
          const item = document.createElement('li');
          appendInlineMarkdown(item, lines[index].replace(/^\s*[-*]\s+/, '').trim());
          list.append(item);
          index++;
        }
        container.append(list);
        continue;
      }
      if (/^\d+[.)]\s+/.test(line)) {
        const list = document.createElement('ol');
        while (index < lines.length && /^\s*\d+[.)]\s+/.test(lines[index])) {
          const item = document.createElement('li');
          appendInlineMarkdown(item, lines[index].replace(/^\s*\d+[.)]\s+/, '').trim());
          list.append(item);
          index++;
        }
        container.append(list);
        continue;
      }
      const paragraph = document.createElement('p');
      const paragraphLines = [];
      while (index < lines.length && lines[index].trim() &&
          !/^(?:#{1,4}\s|[-*]\s|\d+[.)]\s)/.test(lines[index].trim())) {
        paragraphLines.push(lines[index].trim());
        index++;
      }
      appendInlineMarkdown(paragraph, paragraphLines.join(' '));
      container.append(paragraph);
    }
    return container;
  }

  function renderReleaseHistory(releases) {
    const list = document.getElementById('releases-list');
    list.replaceChildren(...releases.map((release) => {
      const item = document.createElement('details');
      item.className = 'release-item';
      const summary = document.createElement('summary');
      const title = document.createElement('span');
      title.className = 'release-item-title';
      title.textContent = release.name || release.tag_name || 'Release';
      const tag = document.createElement('span');
      tag.className = 'release-tag';
      tag.textContent = release.tag_name || '';
      const date = document.createElement('time');
      date.className = 'release-date';
      date.textContent = releaseDate(release.published_at);
      if (release.published_at) date.dateTime = release.published_at;
      summary.append(title, tag, date);
      item.append(summary);
      const content = document.createElement('div');
      content.className = 'release-details';
      const body = typeof release.body === 'string' && release.body.trim() ? release.body.trim() : 'No release notes.';
      const notes = renderMarkdown(body.length > 7000 ? `${body.slice(0, 7000)}\n\nRelease notes truncated.` : body);
      const download = document.createElement('button');
      download.className = 'button release-download';
      download.type = 'button';
      download.textContent = 'Download';
      download.addEventListener('click', () => showDownloadOptions(release));
      content.append(download, notes);
      item.append(content);
      return item;
    }));
  }

  let releasesPromise;
  async function loadReleases() {
    if (releasesPromise) return releasesPromise;
    const status = document.getElementById('releases-status');
    status.textContent = 'Fetching release notes…';
    releasesPromise = (async () => {
      try {
        const response = await fetch(RELEASES_API, { headers: { Accept: 'application/vnd.github+json' } });
        if (!response.ok) throw new Error(`GitHub returned ${response.status}`);
        const data = await response.json();
        if (!Array.isArray(data)) throw new Error('GitHub returned an unexpected response');
        const releases = data.filter((release) => release && release.draft !== true);
        const latest = releases.find((release) => release.prerelease !== true);
        if (!latest) throw new Error('No published release is available');
        document.getElementById('latest-release-tag').textContent = latest.tag_name || '';
        document.getElementById('latest-release-name').textContent = latest.name || '';
        const date = document.getElementById('latest-release-date');
        date.textContent = releaseDate(latest.published_at);
        if (latest.published_at) date.dateTime = latest.published_at;
        document.getElementById('latest-download').onclick = () => showDownloadOptions(latest);
        const latestNotes = document.getElementById('latest-release-notes');
        latestNotes.replaceChildren(renderMarkdown(typeof latest.body === 'string' ? latest.body : 'No release notes.'));
        const previous = releases.filter((release) => release.tag_name !== latest.tag_name);
        renderReleaseHistory(previous);
        status.textContent = `${previous.length} previous releases loaded from GitHub.`;
      } catch (error) {
        releasesPromise = null;
        status.textContent = 'Could not load releases from GitHub. Use the links above to open the release page directly.';
        document.getElementById('latest-release-name').textContent = String(error.message || error);
      }
    })();
    return releasesPromise;
  }

  function checkDownloadsPage() {
    if (location.hash === '#downloads' || location.hash === '#downloads-panel') loadReleases();
  }

  // The community server's figures; the strip stays hidden when it can't be reached.
  async function loadCommunityStats() {
    try {
      const answer = await fetch('https://oceapi.watanoge.com/status', { cache: 'no-store' });
      const community = (await answer.json())?.data?.community;
      if (!community) return;
      const format = (value) => Number(value || 0).toLocaleString();
      document.getElementById('community-registered').textContent = format(community.registered);
      document.getElementById('community-online').textContent = format(community.online);
      document.getElementById('community-hours').textContent = format(community.playedHours);
      document.getElementById('community-stats').hidden = false;
    } catch (error) {
      // offline or blocked: nothing to show
    }
  }

  renderRoadmap();
  renderFeatures();
  loadCommunityStats();
  setInterval(() => { if (!document.hidden) loadCommunityStats(); }, 60000);
  const featureCarousel = document.getElementById('feature-carousel');
  document.getElementById('feature-carousel-close').addEventListener('click', () => featureCarousel.close());
  document.getElementById('feature-carousel-previous').addEventListener('click', () => moveFeatureCarousel(-1));
  document.getElementById('feature-carousel-next').addEventListener('click', () => moveFeatureCarousel(1));
  featureCarousel.addEventListener('click', (event) => {
    if (event.target === featureCarousel) featureCarousel.close();
  });
  featureCarousel.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      event.preventDefault();
      featureCarousel.close();
      return;
    }
    if (event.key === 'ArrowLeft') moveFeatureCarousel(-1);
    if (event.key === 'ArrowRight') moveFeatureCarousel(1);
  });
  document.getElementById('download-modal-close').addEventListener('click', () => {
    document.getElementById('download-modal').close();
  });
  document.getElementById('download-modal').addEventListener('click', (event) => {
    if (event.target === event.currentTarget) event.currentTarget.close();
  });
  window.addEventListener('hashchange', checkDownloadsPage);
  checkDownloadsPage();
})();