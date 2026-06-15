#!/bin/bash
set -e

TRINO_ETC="/etc/trino"
mkdir -p "$TRINO_ETC/catalog"

# --- node.properties ---
# Use HOSTNAME (set by Docker) as unique node ID
cat > "$TRINO_ETC/node.properties" <<EOF
node.environment=production
node.id=$HOSTNAME
EOF

# --- jvm.config ---
# Compute Xmx from TRINO_MEMORY_GB (total container memory in GB).
# TRINO_HEAP_PCT: percentage of container memory for Xmx (default 80)
TOTAL_GB="${TRINO_MEMORY_GB:-10}"
HEAP_PCT="${TRINO_HEAP_PCT:-80}"
HEAP_GB=$(( TOTAL_GB * HEAP_PCT / 100 ))
XMX="${HEAP_GB}g"

cat > "$TRINO_ETC/jvm.config" <<EOF
-server
-Xmx${XMX}
-XX:+UseContainerSupport
-XX:+UseG1GC
-XX:G1HeapRegionSize=32M
-XX:+ExplicitGCInvokesConcurrent
-XX:+HeapDumpOnOutOfMemoryError
-XX:+ExitOnOutOfMemoryError
EOF

# --- config.properties ---
IS_COORDINATOR="${TRINO_COORDINATOR:-false}"

cat > "$TRINO_ETC/config.properties" <<EOF
coordinator=${IS_COORDINATOR}
http-server.http.port=8080
discovery.uri=http://trino-coordinator:8080
EOF

# Coordinator-specific settings
if [ "$IS_COORDINATOR" = "true" ]; then
  cat >> "$TRINO_ETC/config.properties" <<EOF
node-scheduler.include-coordinator=true
EOF
fi

# Memory config: query.max-memory-per-node ≈ 70% of Xmx, query.max-memory = cluster-wide cap
MAX_MEM_PER_NODE="$(( HEAP_GB * 70 / 100 ))GB"
cat >> "$TRINO_ETC/config.properties" <<EOF
query.max-memory-per-node=${MAX_MEM_PER_NODE}
query.max-memory=${TRINO_QUERY_MAX_MEMORY:-50GB}
EOF

# --- catalog configs ---
# Copy any catalog files from the shared template directory
if [ -d /trino-catalogs ]; then
  cp /trino-catalogs/*.properties "$TRINO_ETC/catalog/" 2>/dev/null || true
fi

echo "=== Trino config generated ==="
echo "Node ID:              $HOSTNAME"
echo "Coordinator:          $IS_COORDINATOR"
echo "Container memory:     ${TOTAL_GB}GB"
echo "JVM Xmx:             ${XMX}"
echo "Max memory per node:  ${MAX_MEM_PER_NODE}"
echo "==============================="

# Launch Trino
exec /usr/lib/trino/bin/run-trino
