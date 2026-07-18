#!/usr/bin/env python3
"""
UE5 Remote Execution Protocol Implementation

Socket-based communication with UE5 editor for remote Python execution.
Provides the core UE5RemoteExecution class for discovering and executing
Python code in running UE5 instances.
"""

import json
import logging
import socket
import time
from typing import Optional, Tuple, Dict, Any, List

logger = logging.getLogger(__name__)


class UE5RemoteExecution:
    """
    Execute commands in UE5 editor via Python Remote Execution (socket-based).

    Requires:
    - Python plugin with remote execution enabled
    - UE5 project with proper configuration
    """

    # Protocol constants
    MAGIC = "ue_py"
    PROTOCOL_VERSION = 1
    SOCKET_TIMEOUT = 0.5
    BUFFER_SIZE = 2_097_152

    class ExecTypes:
        EXECUTE_FILE = "ExecuteFile"
        EXECUTE_STATEMENT = "ExecuteStatement"
        EVALUATE_STATEMENT = "EvaluateStatement"

    def __init__(
        self,
        multicast_group: Tuple[str, int] = ("239.0.0.1", 6766),
        multicast_bind_address: str = "0.0.0.0",
        project_name: str = "",
    ):
        self.multicast_group = multicast_group
        self.multicast_bind_address = multicast_bind_address
        self.project_name = project_name
        self.unreal_node_id = None
        self.mcast_sock = None
        self.cmd_sock = None
        self.cmd_connection = None

    def _create_multicast_socket(self) -> socket.socket:
        """Create and configure multicast socket."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
        sock.settimeout(self.SOCKET_TIMEOUT)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 0)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if hasattr(socket, "SO_REUSEPORT"):
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
        sock.bind((self.multicast_bind_address, self.multicast_group[1]))

        membership = socket.inet_aton(self.multicast_group[0])
        bind_addr = socket.inet_aton(self.multicast_bind_address)
        sock.setsockopt(
            socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership + bind_addr
        )

        return sock

    def _send_message(self, sock: socket.socket, message: Dict[str, Any]):
        """Send JSON message via socket."""
        data = json.dumps(message).encode()
        sock.sendto(data, self.multicast_group)

    def _receive_messages(
        self, sock: socket.socket, message_type: str
    ) -> Optional[Dict]:
        """Receive and parse JSON messages from socket."""
        data_received = b""

        try:
            while True:
                try:
                    data, _ = sock.recvfrom(self.BUFFER_SIZE)
                    data_received += data

                    try:
                        json_data = json.loads(data_received)
                        data_received = b""
                    except json.JSONDecodeError:
                        continue

                    if json_data.get("type") == message_type:
                        continue  # Ignore echo

                    # Check project name if specified
                    if self.project_name and "data" in json_data:
                        if json_data["data"].get("project_name") != self.project_name:
                            continue

                    return json_data

                except socket.timeout:
                    break
        except Exception as e:
            logger.error(f"Error receiving message: {e}")

        return None

    def _receive_all_messages(
        self, sock: socket.socket, message_type: str, timeout: float = 1.0
    ) -> List[Dict]:
        """
        Collect ALL responses from multicast socket during timeout window.

        Args:
            sock: Socket to receive from
            message_type: Message type to ignore (our own echo)
            timeout: How long to wait for responses (default 1.0s)

        Returns:
            List of all discovered instance messages (filtered by project name if specified)
        """
        responses = []
        sock.settimeout(0.1)  # Short timeout for each recv
        start_time = time.time()

        try:
            while time.time() - start_time < timeout:
                try:
                    data, _ = sock.recvfrom(self.BUFFER_SIZE)
                    json_data = json.loads(data.decode("utf-8"))

                    if json_data.get("type") == message_type:
                        continue  # Skip echo

                    # Check project name if specified
                    if self.project_name and "data" in json_data:
                        if json_data["data"].get("project_name") != self.project_name:
                            continue

                    # Avoid duplicates by checking node_id
                    if not any(
                        r.get("source") == json_data.get("source") for r in responses
                    ):
                        responses.append(json_data)

                except socket.timeout:
                    continue
                except json.JSONDecodeError:
                    continue
                except Exception as e:
                    logger.debug(f"Error parsing message: {e}")
                    continue
        except Exception as e:
            logger.error(f"Error receiving messages: {e}")

        return responses

    def find_unreal_instance(self) -> bool:
        """Find and connect to running UE5 instance with comprehensive discovery and logging."""
        try:
            self.mcast_sock = self._create_multicast_socket()

            # 1. Start discovery
            logger.info("Searching for UE5 instances...")
            if self.project_name:
                logger.info(f"Filter: project_name='{self.project_name}'")

            # 2. Send ping message
            ping_msg = {
                "version": self.PROTOCOL_VERSION,
                "magic": self.MAGIC,
                "source": "python_executor",
                "type": "ping",
            }

            self._send_message(self.mcast_sock, ping_msg)

            # 3. Collect all responses
            all_pongs = self._receive_all_messages(self.mcast_sock, "ping", timeout=5.0)

            # 4. Log all discovered instances
            if not all_pongs:
                logger.error("No UE5 instances discovered on network")
                logger.info("Ensure UE5 editor is running with Python plugin enabled")
                return False

            logger.info(f"Discovered {len(all_pongs)} UE5 instance(s):")
            for i, pong in enumerate(all_pongs, 1):
                project = pong.get("data", {}).get("project_name", "Unknown")
                engine = pong.get("data", {}).get("engine_version", "Unknown")
                node_id = pong.get("source", "Unknown")
                logger.info(f"  {i}. {project} (UE {engine}) [node: {node_id}]")

            # 5. Select first instance (or use filtering logic from _receive_all_messages)
            selected = all_pongs[0]
            self.unreal_node_id = selected.get("source")
            project = selected.get("data", {}).get("project_name", "Unknown")
            engine = selected.get("data", {}).get("engine_version", "Unknown")

            if len(all_pongs) > 1:
                logger.warning(
                    f"{len(all_pongs)} instances discovered, selected first match"
                )

            logger.info(
                f"Connecting to: {project} (UE {engine}) [node: {self.unreal_node_id}]"
            )
            return True

        except Exception as e:
            logger.error(f"Failed to find UE5: {e}")
            return False

    def find_all_matching_instances(self) -> List[Dict]:
        """
        Find all UE5 instances matching project_name filter.

        Returns:
            List of instance info dicts with 'source' (node_id), 'data' (project info)
        """
        try:
            self.mcast_sock = self._create_multicast_socket()

            logger.info("Searching for UE5 instances...")
            if self.project_name:
                logger.info(f"Filter: project_name='{self.project_name}'")

            ping_msg = {
                "version": self.PROTOCOL_VERSION,
                "magic": self.MAGIC,
                "source": "python_executor",
                "type": "ping",
            }
            self._send_message(self.mcast_sock, ping_msg)

            all_pongs = self._receive_all_messages(self.mcast_sock, "ping", timeout=5.0)

            if all_pongs:
                logger.info(f"Discovered {len(all_pongs)} UE5 instance(s):")
                for i, pong in enumerate(all_pongs, 1):
                    project = pong.get("data", {}).get("project_name", "Unknown")
                    engine = pong.get("data", {}).get("engine_version", "Unknown")
                    node_id = pong.get("source", "Unknown")
                    logger.info(f"  {i}. {project} (UE {engine}) [node: {node_id}]")
            else:
                logger.error("No UE5 instances discovered on network")
                logger.info("Ensure UE5 editor is running with Python plugin enabled")

            return all_pongs

        except Exception as e:
            logger.error(f"Failed to find UE5 instances: {e}")
            return []

    def open_connection(self) -> bool:
        """Open command connection to UE5."""
        try:
            if not self.unreal_node_id:
                logger.error("Must find UE5 instance first")
                return False

            # Get available port
            with socket.socket() as s:
                s.bind(("", 0))
                cmd_port = s.getsockname()[1]

            # Send open connection message
            open_msg = {
                "type": "open_connection",
                "version": self.PROTOCOL_VERSION,
                "magic": self.MAGIC,
                "source": "python_executor",
                "dest": self.unreal_node_id,
                "data": {"command_ip": "127.0.0.1", "command_port": cmd_port},
            }

            self._send_message(self.mcast_sock, open_msg)

            # Create command socket
            self.cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.cmd_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.cmd_sock.bind(("127.0.0.1", cmd_port))
            self.cmd_sock.settimeout(2.0)
            self.cmd_sock.listen()

            self.cmd_connection, _ = self.cmd_sock.accept()
            self.cmd_connection.settimeout(5.0)

            logger.info("Command connection established")
            return True

        except Exception as e:
            logger.error(f"Failed to open connection: {e}")
            return False

    def execute_command(
        self, command: str, exec_type: str = None, timeout: float = 5.0
    ) -> Dict[str, Any]:
        """
        Execute Python command in UE5.

        Args:
            command: Python code or file path
            exec_type: ExecTypes.EXECUTE_FILE, EXECUTE_STATEMENT, or EVALUATE_STATEMENT
            timeout: Timeout for command execution

        Returns:
            Dictionary with execution result
        """
        if exec_type is None:
            exec_type = self.ExecTypes.EVALUATE_STATEMENT

        try:
            # Send command message
            cmd_msg = {
                "type": "command",
                "version": self.PROTOCOL_VERSION,
                "magic": self.MAGIC,
                "source": "python_executor",
                "dest": self.unreal_node_id,
                "data": {
                    "command": command,
                    "unattended": True,
                    "exec_mode": exec_type,
                },
            }

            data = json.dumps(cmd_msg).encode()
            self.cmd_connection.sendto(data, ("127.0.0.1", 0))

            # Receive result
            self.cmd_connection.settimeout(timeout)
            data_received = b""
            result_data = None

            while True:
                try:
                    data, _ = self.cmd_connection.recvfrom(self.BUFFER_SIZE)
                    data_received += data

                    try:
                        json_data = json.loads(data_received)
                        data_received = b""
                    except json.JSONDecodeError:
                        continue

                    if json_data.get("type") == "command":
                        continue  # Ignore echo

                    result_data = json_data
                    break

                except socket.timeout:
                    break

            if result_data:
                success = result_data.get("data", {}).get("success", False)
                result = result_data.get("data", {}).get("result", "")
                output = result_data.get("data", {}).get("output", [])

                logger.info(f"Command executed: {'Success' if success else 'Failed'}")

                return {
                    "success": success,
                    "result": result,
                    "output": output,
                    "raw": result_data,
                }
            else:
                return {"success": False, "error": "No response from UE5", "output": []}

        except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError) as e:
            # Connection errors likely indicate editor crash
            logger.error(f"Connection lost during command execution: {e}")
            return {"success": False, "error": str(e), "crashed": True, "output": []}
        except OSError as e:
            # OS-level socket errors (e.g., "Connection reset by peer")
            if "connection" in str(e).lower() or "broken pipe" in str(e).lower():
                logger.error(f"Connection lost during command execution: {e}")
                return {
                    "success": False,
                    "error": str(e),
                    "crashed": True,
                    "output": [],
                }
            else:
                logger.error(f"Command execution failed: {e}")
                return {
                    "success": False,
                    "error": str(e),
                    "crashed": False,
                    "output": [],
                }
        except Exception as e:
            logger.error(f"Command execution failed: {e}")
            return {"success": False, "error": str(e), "crashed": False, "output": []}

    def get_project_path(self, timeout: float = 5.0) -> Optional[str]:
        """
        Get current project file path from connected UE5 instance.

        Must be called after open_connection().

        Returns:
            Project file path (e.g. "D:/Code/TestFlight/TestFlight.uproject") or None
        """
        code = "import unreal; rel_path = unreal.Paths.get_project_file_path(); print(unreal.Paths.convert_relative_path_to_full(rel_path))"
        result = self.execute_command(
            code, exec_type=self.ExecTypes.EXECUTE_STATEMENT, timeout=timeout
        )

        if result.get("success") and result.get("output"):
            for line in result["output"]:
                if isinstance(line, dict):
                    output_str = line.get("output", "")
                else:
                    output_str = str(line)

                output_str = output_str.strip()
                if output_str and (
                    ".uproject" in output_str or "/" in output_str or "\\" in output_str
                ):
                    return output_str

        return None

    def close_connection(self):
        """Close connection to UE5."""
        try:
            if self.unreal_node_id and self.mcast_sock:
                close_msg = {
                    "type": "close_connection",
                    "version": self.PROTOCOL_VERSION,
                    "magic": self.MAGIC,
                    "source": "python_executor",
                    "dest": self.unreal_node_id,
                }

                self._send_message(self.mcast_sock, close_msg)

            if self.cmd_connection:
                self.cmd_connection.close()
            if self.cmd_sock:
                self.cmd_sock.close()
            if self.mcast_sock:
                self.mcast_sock.close()

            logger.info("Connection closed")
        except Exception as e:
            logger.error(f"Error closing connection: {e}")
