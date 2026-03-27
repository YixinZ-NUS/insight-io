/*
role: browser controller for the task-10 developer control surface.
revision: 2026-03-27 task10-developer-runtime-surface
major changes: adopts the existing route-builder flow for the thin
developer-facing `/api/dev/*` REST facade, adds direct-session management plus
device/stream alias actions, and fixes session-backed rebind handling in the
browser client.
See docs/past-tasks.md.
*/

const state = {
  health: null,
  devices: [],
  apps: [],
  selectedAppId: null,
  selectedApp: null,
  routes: [],
  sources: [],
  status: null,
  selectedSourceIdForRebind: null,
  selectedCatalogUri: '',
  selectedSessionUri: '',
  pollingHandle: null,
};

const elements = {
  refreshAll: document.getElementById('refresh-all'),
  refreshCatalog: document.getElementById('refresh-catalog'),
  healthChip: document.getElementById('health-chip'),
  catalogError: document.getElementById('catalog-error'),
  catalogList: document.getElementById('catalog-list'),
  appForm: document.getElementById('app-form'),
  appName: document.getElementById('app-name'),
  appDescription: document.getElementById('app-description'),
  appFormError: document.getElementById('app-form-error'),
  appCount: document.getElementById('app-count'),
  appList: document.getElementById('app-list'),
  selectedAppName: document.getElementById('selected-app-name'),
  selectedAppMeta: document.getElementById('selected-app-meta'),
  deleteApp: document.getElementById('delete-app'),
  routeForm: document.getElementById('route-form'),
  routeName: document.getElementById('route-name'),
  routeMedia: document.getElementById('route-media'),
  routeSubmit: document.getElementById('route-submit'),
  routeFormError: document.getElementById('route-form-error'),
  sourceForm: document.getElementById('source-form'),
  sourceFormTitle: document.getElementById('source-form-title'),
  sourceFormMode: document.getElementById('source-form-mode'),
  clearSourceForm: document.getElementById('clear-source-form'),
  sourceTarget: document.getElementById('source-target'),
  sourceInput: document.getElementById('source-input'),
  sourceSession: document.getElementById('source-session'),
  sourceRtsp: document.getElementById('source-rtsp'),
  sourceSelectedUri: document.getElementById('source-selected-uri'),
  sourceSubmit: document.getElementById('source-submit'),
  sourceFormError: document.getElementById('source-form-error'),
  sessionForm: document.getElementById('session-form'),
  sessionInput: document.getElementById('session-input'),
  sessionRtsp: document.getElementById('session-rtsp'),
  sessionSelectedUri: document.getElementById('session-selected-uri'),
  sessionSubmit: document.getElementById('session-submit'),
  sessionFormError: document.getElementById('session-form-error'),
  routeList: document.getElementById('route-list'),
  sourceList: document.getElementById('source-list'),
  runtimeSummary: document.getElementById('runtime-summary'),
  runtimeList: document.getElementById('runtime-list'),
  sessionList: document.getElementById('session-list'),
};

async function requestJson(path, options = {}) {
  const response = await fetch(path, {
    headers: {
      'Content-Type': 'application/json',
      ...(options.headers || {}),
    },
    ...options,
  });

  let body = {};
  if (response.status !== 204) {
    const text = await response.text();
    body = text ? JSON.parse(text) : {};
  }

  if (!response.ok) {
    const message = body.message || body.error || `${response.status} ${response.statusText}`;
    throw new Error(message);
  }

  return body;
}

function formatTimestamp(value) {
  if (!value) {
    return 'n/a';
  }
  return new Date(value).toLocaleString();
}

function formatCaps(caps) {
  if (!caps || typeof caps !== 'object') {
    return 'caps unavailable';
  }
  if (caps.width && caps.height) {
    return `${caps.format || 'unknown'} ${caps.width}x${caps.height}${caps.fps ? ` @${caps.fps}fps` : ''}`;
  }
  if (caps.sample_rate) {
    return `${caps.format || 'unknown'} ${caps.sample_rate}Hz${caps.channels ? ` ${caps.channels}ch` : ''}`;
  }
  return caps.format || 'unknown';
}

function suggestTargetForSource(source) {
  if (!source || !source.selector) {
    return '';
  }
  if (source.selector.startsWith('orbbec/preset/')) {
    return 'orbbec';
  }
  if (source.selector.startsWith('orbbec/color/')) {
    return 'orbbec/color';
  }
  if (source.selector.startsWith('orbbec/depth/')) {
    return 'orbbec/depth';
  }
  const selector = source.selector;
  return selector.includes('/') ? selector.split('/').slice(0, -1).join('/') : 'camera';
}

function setError(element, message) {
  element.hidden = !message;
  element.textContent = message || '';
}

async function renameDevice(device) {
  const nextName = window.prompt('Rename device', device.name || device.default_name || '');
  if (nextName === null) {
    return;
  }
  await requestJson(`/api/dev/devices/${encodeURIComponent(device.name)}/alias`, {
    method: 'POST',
    body: JSON.stringify({ name: nextName }),
  });
}

async function renameStream(stream) {
  const nextName = window.prompt('Rename stream', stream.name || stream.default_name || '');
  if (nextName === null) {
    return;
  }
  await requestJson(`/api/dev/streams/${stream.stream_id}/alias`, {
    method: 'POST',
    body: JSON.stringify({ name: nextName }),
  });
}

function clearSessionFormState() {
  elements.sessionInput.value = '';
  elements.sessionRtsp.checked = false;
  state.selectedSessionUri = '';
  elements.sessionSelectedUri.textContent = '';
}

function setSessionFormFromCatalog(source) {
  state.selectedSessionUri = source.uri;
  elements.sessionInput.value = source.uri;
  elements.sessionSelectedUri.textContent = `Selected from catalog: ${source.uri}`;
}

function clearSourceFormState() {
  state.selectedSourceIdForRebind = null;
  elements.sourceFormTitle.textContent = 'Create Source Bind';
  elements.sourceFormMode.textContent =
    'Use a catalog URI or existing session to activate one app-local target.';
  elements.clearSourceForm.hidden = true;
  elements.sourceSubmit.textContent = 'Create source';
  elements.sourceInput.value = '';
  elements.sourceSession.value = '';
  elements.sourceRtsp.checked = false;
  state.selectedCatalogUri = '';
  elements.sourceSelectedUri.textContent = '';
}

function setSourceFormFromCatalog(source) {
  state.selectedCatalogUri = source.uri;
  elements.sourceInput.value = source.uri;
  elements.sourceSession.value = '';
  elements.sourceSelectedUri.textContent = `Selected from catalog: ${source.uri}`;
  if (!elements.sourceTarget.value) {
    elements.sourceTarget.value = suggestTargetForSource(source);
  }
}

function setSourceFormFromSession(session) {
  elements.sourceInput.value = '';
  elements.sourceSession.value = String(session.session_id);
  state.selectedCatalogUri = '';
  elements.sourceSelectedUri.textContent =
    `Selected direct session #${session.session_id}${session.uri ? `: ${session.uri}` : ''}`;
}

function setSourceFormForRebind(source) {
  state.selectedSourceIdForRebind = source.source_id;
  elements.sourceFormTitle.textContent = `Rebind Source #${source.source_id}`;
  elements.sourceFormMode.textContent =
    'Submit a replacement URI or session id for this durable source record.';
  elements.clearSourceForm.hidden = false;
  elements.sourceSubmit.textContent = 'Apply rebind';
  elements.sourceTarget.value = source.target || '';
  if (source.source_session_id) {
    elements.sourceInput.value = '';
    elements.sourceSession.value = String(source.source_session_id);
    elements.sourceSelectedUri.textContent = source.uri
      ? `Current upstream: session:${source.source_session_id} • resolved ${source.uri}`
      : `Current upstream: session:${source.source_session_id}`;
  } else {
    elements.sourceInput.value = source.uri || '';
    elements.sourceSession.value = '';
    elements.sourceSelectedUri.textContent = source.uri
      ? `Current input: ${source.uri}`
      : '';
  }
  elements.sourceRtsp.checked = Boolean(source.rtsp_enabled);
  state.selectedCatalogUri = source.source_session_id ? '' : (source.uri || '');
}

function renderCatalog() {
  elements.catalogList.innerHTML = '';

  if (state.devices.length === 0) {
    elements.catalogList.innerHTML = '<div class="empty-state">No devices are currently published.</div>';
    return;
  }

  for (const device of state.devices) {
    const deviceCard = document.createElement('article');
    deviceCard.className = 'catalog-device';

    const sourceCards = device.streams
      .map((source) => {
        const defaultLabel = source.default_name && source.default_name !== source.name
          ? `<p class="muted">default: ${source.default_name}</p>`
          : '';
        const groupedBadge = source.shape === 'grouped'
          ? '<span class="chip chip-grouped">grouped</span>'
          : '<span class="chip">exact</span>';
        const members = Array.isArray(source.members)
          ? `<div class="members">${source.members
              .map((member) => `<span class="chip">${member.route} → ${member.selector}</span>`)
              .join('')}</div>`
          : '';
        return `
          <div class="source-card" data-uri="${source.uri}">
            <div class="card-title-row">
              <div>
                <h4>${source.name}</h4>
                <p class="muted">${formatCaps(source.caps)}</p>
              </div>
              ${groupedBadge}
            </div>
            <p class="muted">${source.selector}</p>
            ${defaultLabel}
            <code class="uri">${source.uri}</code>
            <div class="source-actions">
              <button type="button" class="ghost-button use-source">Use in source form</button>
              <button type="button" class="ghost-button use-session">Use for session</button>
              <button type="button" class="ghost-button rename-stream">Rename</button>
            </div>
            ${members}
          </div>`;
      })
      .join('');

    const defaultDeviceLabel = device.default_name && device.default_name !== device.name
      ? `<p class="muted">default: ${device.default_name}</p>`
      : '';

    deviceCard.innerHTML = `
      <div class="catalog-device-header">
        <div>
          <p class="eyebrow">${device.driver}</p>
          <h3>${device.name}</h3>
          ${defaultDeviceLabel}
          <p class="muted">${device.streams.length} usable stream URI(s)</p>
        </div>
        <div class="source-actions">
          <button type="button" class="ghost-button rename-device">Rename device</button>
        </div>
      </div>
      <div class="source-grid">${sourceCards}</div>
    `;

    deviceCard.querySelector('.rename-device').addEventListener('click', async () => {
      try {
        await renameDevice(device);
        await refreshAll();
      } catch (error) {
        setError(elements.catalogError, error.message);
      }
    });

    deviceCard.querySelectorAll('.use-source').forEach((button) => {
      button.addEventListener('click', () => {
        const uri = button.closest('.source-card').dataset.uri;
        const source = device.streams.find((entry) => entry.uri === uri);
        if (source) {
          setSourceFormFromCatalog(source);
        }
      });
    });

    deviceCard.querySelectorAll('.use-session').forEach((button) => {
      button.addEventListener('click', () => {
        const uri = button.closest('.source-card').dataset.uri;
        const source = device.streams.find((entry) => entry.uri === uri);
        if (source) {
          setSessionFormFromCatalog(source);
        }
      });
    });

    deviceCard.querySelectorAll('.rename-stream').forEach((button) => {
      button.addEventListener('click', async () => {
        const uri = button.closest('.source-card').dataset.uri;
        const source = device.streams.find((entry) => entry.uri === uri);
        if (!source) {
          return;
        }
        try {
          await renameStream(source);
          await refreshAll();
        } catch (error) {
          setError(elements.catalogError, error.message);
        }
      });
    });

    elements.catalogList.appendChild(deviceCard);
  }
}

function renderAppList() {
  elements.appCount.textContent = String(state.apps.length);
  elements.appList.innerHTML = '';

  if (state.apps.length === 0) {
    elements.appList.innerHTML = '<div class="empty-state">No persisted apps yet.</div>';
    return;
  }

  for (const app of state.apps) {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `list-item-button${state.selectedAppId === app.app_id ? ' active' : ''}`;
    button.innerHTML = `
      <span>${app.name}</span>
      <span class="muted">#${app.app_id}</span>
    `;
    button.addEventListener('click', async () => {
      state.selectedAppId = app.app_id;
      await loadSelectedApp();
      render();
    });
    elements.appList.appendChild(button);
  }
}

function renderSelectedApp() {
  const hasSelection = Boolean(state.selectedApp);
  elements.routeSubmit.disabled = !hasSelection;
  elements.sourceSubmit.disabled = !hasSelection;
  elements.deleteApp.disabled = !hasSelection;

  if (!hasSelection) {
    elements.selectedAppName.textContent = 'Select an app';
    elements.selectedAppMeta.textContent =
      'Create or pick an app to manage routes and sources.';
    elements.routeList.textContent = 'No routes yet.';
    elements.sourceList.textContent = 'No sources yet.';
    clearSourceFormState();
    elements.routeSubmit.disabled = true;
    elements.sourceSubmit.disabled = true;
    elements.deleteApp.disabled = true;
    return;
  }

  const app = state.selectedApp;
  elements.selectedAppName.textContent = app.name;
  elements.selectedAppMeta.textContent =
    `App #${app.app_id} • updated ${formatTimestamp(app.updated_at_ms)}`;

  elements.routeList.innerHTML = '';
  if (state.routes.length === 0) {
    elements.routeList.innerHTML = '<div class="empty-state">No routes declared.</div>';
  } else {
    for (const route of state.routes) {
      const routeRow = document.createElement('div');
      routeRow.className = 'detail-row';
      routeRow.innerHTML = `
        <div>
          <strong>${route.name}</strong>
          <p class="muted">${route.media || 'no expectation'}</p>
        </div>
        <button type="button" class="ghost-button danger delete-route">Delete</button>
      `;
      routeRow.querySelector('.delete-route').addEventListener('click', async () => {
        try {
          await requestJson(`/api/dev/apps/${app.app_id}/routes/${encodeURIComponent(route.name)}`, {
            method: 'DELETE',
          });
          await loadSelectedApp();
          await loadStatus();
          render();
        } catch (error) {
          setError(elements.routeFormError, error.message);
        }
      });
      elements.routeList.appendChild(routeRow);
    }
  }

  elements.sourceList.innerHTML = '';
  if (state.sources.length === 0) {
    elements.sourceList.innerHTML = '<div class="empty-state">No sources bound.</div>';
  } else {
    for (const source of state.sources) {
      const row = document.createElement('div');
      row.className = 'detail-row detail-row-source';
      const upstreamLabel = source.source_session_id
        ? `session:${source.source_session_id}`
        : (source.uri || 'no upstream');
      const resolvedLabel = source.source_session_id && source.uri
        ? `<p class="muted">resolved ${source.uri}</p>`
        : '';
      const memberChips = Array.isArray(source.members)
        ? source.members
            .map((member) => `<span class="chip">${member.route} → ${member.selector}</span>`)
            .join('')
        : '';
      row.innerHTML = `
        <div>
          <div class="card-title-row">
            <strong>${source.target}</strong>
            <span class="chip ${source.state === 'active' ? 'chip-active' : ''}">${source.state}</span>
          </div>
          <p class="muted">Source #${source.source_id} • stream #${source.stream_id}</p>
          <p class="muted">${upstreamLabel}</p>
          ${resolvedLabel}
          <p class="muted">${source.device ? `${source.device} / ${source.stream}` : ''}</p>
          ${memberChips ? `<div class="members">${memberChips}</div>` : ''}
        </div>
        <div class="row-actions">
          <button type="button" class="ghost-button source-edit">Rebind</button>
          <button type="button" class="ghost-button source-toggle">${source.state === 'active' ? 'Stop' : 'Start'}</button>
        </div>
      `;

      row.querySelector('.source-edit').addEventListener('click', () => {
        setSourceFormForRebind(source);
      });
      row.querySelector('.source-toggle').addEventListener('click', async () => {
        const action = source.state === 'active' ? 'stop' : 'start';
        try {
          await requestJson(`/api/dev/apps/${app.app_id}/sources/${source.source_id}:${action}`, {
            method: 'POST',
            body: JSON.stringify({}),
          });
          await loadSelectedApp();
          await loadStatus();
          render();
        } catch (error) {
          setError(elements.sourceFormError, error.message);
        }
      });

      elements.sourceList.appendChild(row);
    }
  }
}

function renderRuntime() {
  const snapshot = state.status || {
    total_sessions: 0,
    active_sessions: 0,
    stopped_sessions: 0,
    total_serving_runtimes: 0,
    sessions: [],
    serving_runtimes: [],
  };

  const metrics = [
    ['logical sessions', snapshot.total_sessions],
    ['active sessions', snapshot.active_sessions],
    ['stopped sessions', snapshot.stopped_sessions],
    ['serving runtimes', snapshot.total_serving_runtimes],
  ];
  elements.runtimeSummary.innerHTML = metrics
    .map(([label, value]) => `
      <div class="metric-card">
        <strong>${value}</strong>
        <span>${label}</span>
      </div>
    `)
    .join('');

  elements.runtimeList.innerHTML = '';
  if (!snapshot.serving_runtimes || snapshot.serving_runtimes.length === 0) {
    elements.runtimeList.innerHTML = '<div class="empty-state">No serving runtimes are active.</div>';
  } else {
    for (const runtime of snapshot.serving_runtimes) {
      const runtimeCard = document.createElement('div');
      runtimeCard.className = 'detail-row runtime-row';
      const memberChips = (runtime.members || [])
        .map((member) => `<span class="chip">${member.route || member.selector}</span>`)
        .join('');
      runtimeCard.innerHTML = `
        <div>
          <strong>${runtime.uri || 'runtime'}</strong>
          <p class="muted">owner session #${runtime.owner_session_id} • consumers ${runtime.consumer_session_ids?.join(', ') || 'none'}</p>
          <p class="muted">${runtime.ipc_socket_path || 'no IPC socket'}${runtime.rtsp_url ? ` • ${runtime.rtsp_url}` : ''}</p>
          ${memberChips ? `<div class="members">${memberChips}</div>` : ''}
        </div>
      `;
      elements.runtimeList.appendChild(runtimeCard);
    }
  }

  elements.sessionList.innerHTML = '';
  if (!snapshot.sessions || snapshot.sessions.length === 0) {
    elements.sessionList.innerHTML = '<div class="empty-state">No logical sessions are active.</div>';
  } else {
    for (const session of snapshot.sessions) {
      const row = document.createElement('div');
      row.className = 'detail-row';
      const actionButtons = session.kind === 'direct'
        ? `
          <button type="button" class="ghost-button session-attach">Use in source form</button>
          <button type="button" class="ghost-button session-toggle">${session.state === 'active' ? 'Stop' : 'Start'}</button>
          ${session.state === 'stopped' ? '<button type="button" class="ghost-button danger session-delete">Delete</button>' : ''}
        `
        : '';
      row.innerHTML = `
        <div>
          <strong>Session #${session.session_id}</strong>
          <p class="muted">${session.kind} • ${session.state}</p>
          <p class="muted">${session.requested_uri || session.uri || 'no user-facing handle'}</p>
          <p class="muted">${session.device ? `${session.device} / ${session.stream}` : ''}</p>
          ${session.rtsp_url ? `<p class="muted">${session.rtsp_url}</p>` : ''}
        </div>
        <div class="row-actions">${actionButtons}</div>
      `;

      if (session.kind === 'direct') {
        row.querySelector('.session-attach').addEventListener('click', () => {
          setSourceFormFromSession(session);
        });
        row.querySelector('.session-toggle').addEventListener('click', async () => {
          const action = session.state === 'active' ? 'stop' : 'start';
          try {
            await requestJson(`/api/dev/sessions/${session.session_id}:${action}`, {
              method: 'POST',
              body: JSON.stringify({}),
            });
            await loadSelectedApp();
            await loadStatus();
            render();
          } catch (error) {
            setError(elements.sessionFormError, error.message);
          }
        });
        const deleteButton = row.querySelector('.session-delete');
        if (deleteButton) {
          deleteButton.addEventListener('click', async () => {
            try {
              await requestJson(`/api/dev/sessions/${session.session_id}`, {
                method: 'DELETE',
              });
              await loadSelectedApp();
              await loadStatus();
              render();
            } catch (error) {
              setError(elements.sessionFormError, error.message);
            }
          });
        }
      }

      elements.sessionList.appendChild(row);
    }
  }
}

function renderHealth() {
  const online = Boolean(state.health && state.health.status === 'ok');
  elements.healthChip.textContent = online ? 'online' : 'offline';
  elements.healthChip.className = `health-chip ${online ? 'online' : 'offline'}`;
}

function render() {
  renderHealth();
  renderCatalog();
  renderAppList();
  renderSelectedApp();
  renderRuntime();
}

async function loadHealth() {
  state.health = await requestJson('/api/dev/health');
}

async function loadCatalog(refresh = false) {
  if (refresh) {
    state.devices = (await requestJson('/api/dev/catalog:refresh', {
      method: 'POST',
      body: JSON.stringify({}),
    })).devices || [];
  } else {
    state.devices = (await requestJson('/api/dev/catalog')).devices || [];
  }
}

async function loadApps() {
  state.apps = (await requestJson('/api/dev/apps')).apps || [];
  if (state.selectedAppId && !state.apps.some((app) => app.app_id === state.selectedAppId)) {
    state.selectedAppId = null;
    state.selectedApp = null;
    state.routes = [];
    state.sources = [];
  }
  if (!state.selectedAppId && state.apps.length > 0) {
    state.selectedAppId = state.apps[0].app_id;
  }
}

async function loadSelectedApp() {
  if (!state.selectedAppId) {
    state.selectedApp = null;
    state.routes = [];
    state.sources = [];
    return;
  }
  state.selectedApp = await requestJson(`/api/dev/apps/${state.selectedAppId}`);
  state.routes = state.selectedApp.routes || [];
  state.sources = state.selectedApp.sources || [];
}

async function loadStatus() {
  state.status = await requestJson('/api/dev/runtime');
}

async function refreshAll({ refreshCatalog = false } = {}) {
  try {
    setError(elements.catalogError, '');
    await loadHealth();
    await loadCatalog(refreshCatalog);
    await loadApps();
    await loadSelectedApp();
    await loadStatus();
    render();
  } catch (error) {
    setError(elements.catalogError, error.message);
    state.health = null;
    renderHealth();
  }
}

elements.refreshAll.addEventListener('click', async () => {
  await refreshAll();
});

elements.refreshCatalog.addEventListener('click', async () => {
  await refreshAll({ refreshCatalog: true });
});

elements.sessionForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  setError(elements.sessionFormError, '');

  const input = elements.sessionInput.value.trim();
  if (!input) {
    setError(elements.sessionFormError, 'Input URI is required.');
    return;
  }

  try {
    await requestJson('/api/dev/sessions', {
      method: 'POST',
      body: JSON.stringify({
        input,
        rtsp_enabled: Boolean(elements.sessionRtsp.checked),
      }),
    });
    clearSessionFormState();
    await loadStatus();
    render();
  } catch (error) {
    setError(elements.sessionFormError, error.message);
  }
});

elements.appForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  setError(elements.appFormError, '');
  try {
    const created = await requestJson('/api/dev/apps', {
      method: 'POST',
      body: JSON.stringify({
        name: elements.appName.value,
        description: elements.appDescription.value,
      }),
    });
    elements.appName.value = '';
    elements.appDescription.value = '';
    state.selectedAppId = created.app_id;
    await refreshAll();
  } catch (error) {
    setError(elements.appFormError, error.message);
  }
});

elements.deleteApp.addEventListener('click', async () => {
  if (!state.selectedAppId) {
    return;
  }
  try {
    await requestJson(`/api/dev/apps/${state.selectedAppId}`, {
      method: 'DELETE',
    });
    state.selectedAppId = null;
    clearSourceFormState();
    await refreshAll();
  } catch (error) {
    setError(elements.appFormError, error.message);
  }
});

elements.routeForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  if (!state.selectedAppId) {
    return;
  }
  setError(elements.routeFormError, '');
  try {
    await requestJson(`/api/dev/apps/${state.selectedAppId}/routes`, {
      method: 'POST',
      body: JSON.stringify({
        name: elements.routeName.value,
        media: elements.routeMedia.value,
      }),
    });
    elements.routeName.value = '';
    await loadSelectedApp();
    render();
  } catch (error) {
    setError(elements.routeFormError, error.message);
  }
});

elements.clearSourceForm.addEventListener('click', () => {
  clearSourceFormState();
});

elements.sourceForm.addEventListener('submit', async (event) => {
  event.preventDefault();
  if (!state.selectedAppId) {
    return;
  }
  setError(elements.sourceFormError, '');

  const inputValue = elements.sourceInput.value.trim();
  const sessionRaw = elements.sourceSession.value.trim();

  if (!inputValue && !sessionRaw) {
    setError(elements.sourceFormError, 'Either input URI or session id is required.');
    return;
  }

  if (inputValue && sessionRaw) {
    setError(elements.sourceFormError, 'Provide either input URI or session id, not both.');
    return;
  }

  let sessionId = null;
  if (sessionRaw) {
    if (!/^[1-9]\d*$/.test(sessionRaw)) {
      setError(elements.sourceFormError, 'Session id must be a positive integer.');
      return;
    }
    sessionId = Number.parseInt(sessionRaw, 10);
  }

  const payload = {
    target: elements.sourceTarget.value,
  };
  if (inputValue) {
    payload.input = inputValue;
  } else if (sessionId !== null) {
    payload.session_id = sessionId;
  }
  if (elements.sourceRtsp.checked) {
    payload.rtsp_enabled = true;
  }

  try {
    if (state.selectedSourceIdForRebind) {
      await requestJson(
        `/api/dev/apps/${state.selectedAppId}/sources/${state.selectedSourceIdForRebind}:rebind`,
        {
          method: 'POST',
          body: JSON.stringify(payload),
        },
      );
    } else {
      await requestJson(`/api/dev/apps/${state.selectedAppId}/sources`, {
        method: 'POST',
        body: JSON.stringify(payload),
      });
    }

    clearSourceFormState();
    await loadSelectedApp();
    await loadStatus();
    render();
  } catch (error) {
    setError(elements.sourceFormError, error.message);
  }
});

window.addEventListener('beforeunload', () => {
  if (state.pollingHandle) {
    window.clearInterval(state.pollingHandle);
  }
});

async function boot() {
  clearSessionFormState();
  clearSourceFormState();
  render();
  await refreshAll();
  state.pollingHandle = window.setInterval(() => {
    refreshAll().catch(() => {});
  }, 2000);
}

boot().catch((error) => {
  setError(elements.catalogError, error.message);
});
