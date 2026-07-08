#!/usr/bin/env python3
"""
Basic RESTCONF Test Case for oven.yang

This simple test demonstrates basic RESTCONF operations on the oven module.
It serves as a pedagogical example and a starting point for testing.

Module: oven.yang (urn:sysrepo:oven)
Server: Any RESTCONF server with oven.yang loaded (e.g., Sysrepo with sysrepocfg)

Requirements:
- requests library (pip install requests)
- A running RESTCONF server with oven.yang loaded
"""

import json
import sys
from typing import Optional, Dict, Any


class OvenRestconfTest:
    """Simple RESTCONF test client for oven.yang module."""
    
    def __init__(self, base_url: str = "http://localhost:8080/restconf", 
                 username: str = "admin", password: str = "admin"):
        """
        Initialize the test client.
        
        Args:
            base_url: Base URL of the RESTCONF server
            username: Username for authentication
            password: Password for authentication
        """
        self.base_url = base_url.rstrip('/')
        self.auth = (username, password)
        self.headers = {
            'Accept': 'application/yang-data+json',
            'Content-Type': 'application/yang-data+json',
        }
        
    def _request(self, method: str, path: str, data: Optional[Dict] = None) -> Dict[str, Any]:
        """
        Send a RESTCONF request.
        
        Args:
            method: HTTP method (GET, POST, PUT, PATCH, DELETE)
            path: RESTCONF path (e.g., '/data/oven:oven')
            data: JSON data for POST/PUT/PATCH
            
        Returns:
            Dictionary with response data
            
        Raises:
            Exception: If the request fails
        """
        url = f"{self.base_url}{path}"
        
        try:
            if method == 'GET':
                response = self._get(url)
            elif method == 'POST':
                response = self._post(url, data)
            elif method == 'PUT':
                response = self._put(url, data)
            elif method == 'PATCH':
                response = self._patch(url, data)
            elif method == 'DELETE':
                response = self._delete(url)
            else:
                raise ValueError(f"Unsupported HTTP method: {method}")
            
            return {
                'status': response.status_code,
                'data': self._parse_response(response),
                'headers': dict(response.headers),
            }
        except Exception as e:
            raise Exception(f"Request failed for {url}: {str(e)}")
    
    def _get(self, url: str) -> Any:
        """Send GET request."""
        import requests
        return requests.get(url, auth=self.auth, headers=self.headers, timeout=10)
    
    def _post(self, url: str, data: Optional[Dict]) -> Any:
        """Send POST request."""
        import requests
        return requests.post(url, auth=self.auth, headers=self.headers, 
                            data=json.dumps(data), timeout=10)
    
    def _put(self, url: str, data: Optional[Dict]) -> Any:
        """Send PUT request."""
        import requests
        return requests.put(url, auth=self.auth, headers=self.headers,
                           data=json.dumps(data), timeout=10)
    
    def _patch(self, url: str, data: Optional[Dict]) -> Any:
        """Send PATCH request."""
        import requests
        return requests.patch(url, auth=self.auth, headers=self.headers,
                             data=json.dumps(data), timeout=10)
    
    def _delete(self, url: str) -> Any:
        """Send DELETE request."""
        import requests
        return requests.delete(url, auth=self.auth, headers=self.headers, timeout=10)
    
    def _parse_response(self, response: Any) -> Dict[str, Any]:
        """Parse the response body."""
        try:
            return response.json()
        except ValueError:
            return {'raw': response.text}

    # ========================================================================
    # Test Cases for oven.yang
    # ========================================================================
    
    def test_001_discovery(self) -> bool:
        """
        TC-001: RESTCONF Discovery (RFC 8040 Section 2.2)
        
        Verify that the server supports RESTCONF by checking the
        /.well-known/host-meta endpoint.
        
        Expected: 200 OK with XML containing RESTCONF entry
        """
        print("\n[TC-001] RESTCONF Discovery")
        print("  Checking /.well-known/host-meta...")
        
        try:
            response = self._get(f"{self.base_url}/.well-known/host-meta")
            if response.status_code == 200:
                print("  ✓ PASS: Server supports RESTCONF discovery")
                return True
            else:
                print(f"  ✗ FAIL: Expected 200, got {response.status_code}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_002_root_resource(self) -> bool:
        """
        TC-002: Root Resource Access (RFC 8040 Section 3.1)
        
        Verify that the RESTCONF root resource is accessible.
        
        Expected: 200 OK with API resource list
        """
        print("\n[TC-002] Root Resource Access")
        print("  Checking /restconf...")
        
        try:
            result = self._request('GET', '/')
            if result['status'] == 200:
                print("  ✓ PASS: Root resource accessible")
                return True
            else:
                print(f"  ✗ FAIL: Expected 200, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_003_get_oven_config(self) -> bool:
        """
        TC-003: GET Oven Configuration (RFC 8040 Section 3.3)
        
        Read the oven configuration container.
        
        Expected: 200 OK with oven configuration data
        """
        print("\n[TC-003] GET Oven Configuration")
        print("  Reading /restconf/data/oven:oven...")
        
        try:
            result = self._request('GET', '/data/oven:oven')
            if result['status'] == 200:
                print(f"  ✓ PASS: Retrieved oven config")
                print(f"  Data: {json.dumps(result['data'], indent=4)}")
                return True
            else:
                print(f"  ✗ FAIL: Expected 200, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_004_get_oven_state(self) -> bool:
        """
        TC-004: GET Oven State Data (RFC 8040 Section 3.3)
        
        Read the oven state (operational) data.
        
        Expected: 200 OK with oven-state data (config=false container)
        """
        print("\n[TC-004] GET Oven State Data")
        print("  Reading /restconf/data/oven:oven-state...")
        
        try:
            result = self._request('GET', '/data/oven:oven-state')
            if result['status'] == 200:
                print(f"  ✓ PASS: Retrieved oven state")
                print(f"  Data: {json.dumps(result['data'], indent=4)}")
                return True
            else:
                print(f"  ✗ FAIL: Expected 200, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_005_put_oven_config(self) -> bool:
        """
        TC-005: PUT Oven Configuration (RFC 8040 Section 3.4)
        
        Create or replace the oven configuration.
        
        Expected: 201 Created or 204 No Content
        """
        print("\n[TC-005] PUT Oven Configuration")
        
        config = {
            "oven:oven": {
                "turned-on": True,
                "temperature": 180
            }
        }
        
        print(f"  Setting oven config: {json.dumps(config, indent=2)}")
        
        try:
            result = self._request('PUT', '/data/oven:oven', config)
            if result['status'] in [201, 204]:
                print(f"  ✓ PASS: Configuration updated (status: {result['status']})")
                return True
            else:
                print(f"  ✗ FAIL: Expected 201 or 204, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_006_patch_oven_temperature(self) -> bool:
        """
        TC-006: PATCH Oven Temperature (RFC 8040 Section 3.5)
        
        Modify only the temperature leaf in the oven configuration.
        
        Expected: 204 No Content
        """
        print("\n[TC-006] PATCH Oven Temperature")
        
        patch = {
            "oven:oven": {
                "temperature": 200
            }
        }
        
        print(f"  Updating temperature to 200...")
        
        try:
            result = self._request('PATCH', '/data/oven:oven', patch)
            if result['status'] == 204:
                print("  ✓ PASS: Temperature updated")
                return True
            else:
                print(f"  ✗ FAIL: Expected 204, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_007_delete_oven_config(self) -> bool:
        """
        TC-007: DELETE Oven Configuration (RFC 8040 Section 3.6)
        
        Delete the oven configuration (reset to defaults).
        
        Expected: 204 No Content
        """
        print("\n[TC-007] DELETE Oven Configuration")
        print("  Deleting oven configuration...")
        
        try:
            result = self._request('DELETE', '/data/oven:oven')
            if result['status'] == 204:
                print("  ✓ PASS: Configuration deleted")
                return True
            else:
                print(f"  ✗ FAIL: Expected 204, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_008_rpc_insert_food(self) -> bool:
        """
        TC-008: RPC - Insert Food (RFC 8040 Section 3.7)
        
        Execute the insert-food RPC operation.
        
        Expected: 200 OK with RPC output
        """
        print("\n[TC-008] RPC - Insert Food")
        
        rpc_data = {
            "oven:input": {
                "time": "now"
            }
        }
        
        print("  Executing insert-food RPC with time=now...")
        
        try:
            result = self._request('POST', '/operations/oven:insert-food', rpc_data)
            if result['status'] == 200:
                print(f"  ✓ PASS: RPC executed successfully")
                return True
            else:
                print(f"  ✗ FAIL: Expected 200, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_009_rpc_remove_food(self) -> bool:
        """
        TC-009: RPC - Remove Food (RFC 8040 Section 3.7)
        
        Execute the remove-food RPC operation.
        
        Expected: 200 OK with RPC output
        """
        print("\n[TC-009] RPC - Remove Food")
        print("  Executing remove-food RPC...")
        
        try:
            result = self._request('POST', '/operations/oven:remove-food')
            if result['status'] == 200:
                print("  ✓ PASS: RPC executed successfully")
                return True
            else:
                print(f"  ✗ FAIL: Expected 200, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def test_010_get_with_filter(self) -> bool:
        """
        TC-010: GET with Filter (RFC 8040 Section 4.8.2)
        
        Read only the turned-on leaf from the oven configuration.
        
        Expected: 200 OK with filtered data
        """
        print("\n[TC-010] GET with Filter")
        print("  Reading /restconf/data/oven:oven/turned-on...")
        
        try:
            result = self._request('GET', '/data/oven:oven/turned-on')
            if result['status'] == 200:
                print(f"  ✓ PASS: Retrieved single leaf")
                print(f"  turned-on = {result['data']}")
                return True
            else:
                print(f"  ✗ FAIL: Expected 200, got {result['status']}")
                return False
        except Exception as e:
            print(f"  ✗ FAIL: {str(e)}")
            return False

    def run_all_tests(self) -> Dict[str, Any]:
        """Run all test cases and return results."""
        tests = [
            self.test_001_discovery,
            self.test_002_root_resource,
            self.test_003_get_oven_config,
            self.test_004_get_oven_state,
            self.test_005_put_oven_config,
            self.test_006_patch_oven_temperature,
            self.test_007_delete_oven_config,
            self.test_008_rpc_insert_food,
            self.test_009_rpc_remove_food,
            self.test_010_get_with_filter,
        ]
        
        results = {
            'total': len(tests),
            'passed': 0,
            'failed': 0,
            'details': [],
        }
        
        print("\n" + "=" * 60)
        print("  RESTCONF Basic Test Suite - oven.yang")
        print("=" * 60)
        
        for test in tests:
            test_name = test.__name__
            try:
                passed = test()
                results['details'].append({
                    'test': test_name,
                    'passed': passed,
                })
                if passed:
                    results['passed'] += 1
                else:
                    results['failed'] += 1
            except Exception as e:
                results['details'].append({
                    'test': test_name,
                    'passed': False,
                    'error': str(e),
                })
                results['failed'] += 1
        
        print("\n" + "=" * 60)
        print(f"  Results: {results['passed']}/{results['total']} tests passed")
        print("=" * 60)
        
        return results


def main():
    """Main entry point for the test suite."""
    import argparse
    
    parser = argparse.ArgumentParser(
        description='RESTCONF Basic Test Suite for oven.yang'
    )
    parser.add_argument(
        '--url',
        default='http://localhost:8080/restconf',
        help='Base URL of the RESTCONF server (default: http://localhost:8080/restconf)'
    )
    parser.add_argument(
        '--user',
        default='admin',
        help='Username for authentication (default: admin)'
    )
    parser.add_argument(
        '--password',
        default='admin',
        help='Password for authentication (default: admin)'
    )
    parser.add_argument(
        '--test',
        type=str,
        help='Run a specific test case (e.g., test_003_get_oven_config)'
    )
    parser.add_argument(
        '--list',
        action='store_true',
        help='List all available test cases'
    )
    
    args = parser.parse_args()
    
    test_client = OvenRestconfTest(args.url, args.user, args.password)
    
    if args.list:
        print("Available test cases:")
        tests = [m for m in dir(test_client) if m.startswith('test_')]
        for test in tests:
            doc = getattr(test_client, test).__doc__
            if doc:
                lines = doc.strip().split('\n')
                print(f"\n  {test}:")
                print(f"    {lines[0] if len(lines) > 0 else ''}")
        sys.exit(0)
    
    if args.test:
        test_method = getattr(test_client, args.test, None)
        if test_method:
            test_method()
        else:
            print(f"Error: Test case '{args.test}' not found")
            sys.exit(1)
    else:
        results = test_client.run_all_tests()
        
        if results['failed'] > 0:
            sys.exit(1)
        else:
            sys.exit(0)


if __name__ == '__main__':
    main()
